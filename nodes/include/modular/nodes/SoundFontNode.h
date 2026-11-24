#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "modular/Graph.h"
#include "modular/Node.h"
#include "Payloads.h"
#include "SoundFont.h"

namespace modular::nodes {

// A SoundFont 2 instrument: notes on one event<midi> input, stereo on one
// audio<2> output. It owns the loaded file as its stored content and plays one
// preset of it on every MIDI channel, from a fixed pool of internal voices.
//
// A voice is one sounding sample: every instrument zone a note matches plays
// as its own mono voice, placed by its pan. A note-off starts a voice's
// release and frees nothing; a voice ends by itself, when its envelope has
// fallen 96 dB after its attack or its sample has run out with no loop left.
// When too few voices are free, a note-on takes whole notes, released before
// held and oldest first, each fading out over 6 ms. A key struck again starts
// a new note beside the old one, and its note-offs end its held notes oldest
// first, whatever has become of their voices.
//
// Loading parses the whole file at authoring time. prepare() never reads the
// file again: it derives what depends on the sample rate and resets the pool,
// so a fresh Engine on the same graph renders from a clean start.
class SoundFontNode : public Node {
public:
  static constexpr std::uint32_t kDefaultVoiceLimit = 256;

  explicit SoundFontNode(Graph& graph);
  SoundFontNode(Graph& graph, SoundFont soundFont);

  PortId midiIn() const { return midi_; }
  PortId out() const { return out_; }

  bool ready() const { return sound_font_.has_value(); }
  // The loaded file, its bytes the node's stored content. Throws while empty.
  const SoundFont& soundFont() const {
    if (!ready()) throw std::invalid_argument("soundfont: load a soundfont first");
    return *sound_font_;
  }
  // Edit-only. A failed load throws and keeps the previous content. A load
  // selects the file's lowest preset and keeps the voice limit.
  void load(std::span<const std::uint8_t> bytes);
  void load(const std::filesystem::path& path);
  // What the loaded file uses that the node ignores or skips, built at load,
  // each kind once; empty while the node is empty. process() never reads it.
  std::span<const std::string> ignored() const {
    if (!ready()) return {};
    return sound_font_->ignored();
  }

  // The selected preset; zero while the node is empty.
  std::uint16_t bank() const { return preset_.bank; }
  std::uint16_t program() const { return preset_.program; }
  // Edit-only. Throws for a preset the loaded file lacks.
  void setPreset(std::uint16_t bank, std::uint16_t program);

  std::uint32_t voiceLimit() const { return voice_limit_; }
  // Edit-only. Accepts 1 to 65535.
  void setVoiceLimit(std::uint32_t limit);

  void prepare(const PrepareInfo& info) override;
  void process(ProcessContext& ctx) override;

private:
  enum class Stage : std::uint8_t {
    kDelay, kAttack, kHold, kDecay, kSustain, kRelease, kOff
  };

  // How many frames an envelope's timed stages last. prepare() derives the
  // delay, the attack and the sustain; a note-on, the hold and the decay,
  // which key scaling makes depend on the key.
  struct Timeline {
    std::uint64_t delay = 0, attack = 0, hold = 0;
    std::uint64_t decay = 0; // frames from the peak to the sustain or the end
    bool decayEnds = false;  // the decay ends the envelope instead of sustaining
    double sustain = 1;      // the level the decay falls to
  };

  // Where an envelope is. Both envelopes share their stages' timing; they
  // differ in how their decay and release move the level.
  struct Envelope {
    Timeline timeline;
    Stage stage = Stage::kDelay;
    std::uint64_t frame = 0;         // frames into the stage
    std::uint64_t releaseFrames = 0; // the release's length, from its start
    double level = 0; // in decay, sustain and release: the volume's level for
                      // the next frame, or the modulation's at the release start
    // Passes over finished stages, so the stage holds the next frame. False
    // once the envelope is off.
    bool settle();
    void release(double from, std::uint64_t frames);
  };

  // A low-frequency oscillator, the specification's triangle: 0 through its
  // delay, then up from 0 to 1, down to -1 and back to 0 every period.
  struct Lfo {
    std::uint64_t delay = 0; // frames left before it starts
    double phase = 0;        // where it is in its period, from 0 to 1
    double value() const;    // this frame's
    void advance(double step); // to the next frame, step being periods per frame
  };

  // The voice's two-pole low-pass, a state-variable filter whose coefficients
  // follow the cutoff in whole cents.
  struct Filter {
    std::optional<std::int64_t> cents; // the cutoff the coefficients are for
    double a1 = 0, a2 = 0, a3 = 0;
    double state1 = 0, state2 = 0;
    void tune(std::int64_t cutoff, double rate, double damping);
    double step(double in);
  };

  // One zone of the selected preset, with what depends on the sample rate.
  struct PreparedZone {
    SoundFont::Zone zone;
    int root = 60;
    double rateRatio = 1; // sample rate over graph rate
    int tuning = 0;       // cents: coarseTune, fineTune and the header's correction
    double panLeft = 0, panRight = 0;
    double attenuation = 0; // centibels: initialAttenuation plus held controllers
    double damping = 0;     // the filter's 1 / Q
    double filterGain = 1;  // the filter's gain at DC
    // Points from the zone's start: those it plays, and its loop.
    std::size_t points = 0, loopStart = 0, loopEnd = 0;
    bool loops = false;          // loops while held
    bool loopsInRelease = false; // and through its release
    Timeline volume, modulation; // all but the hold and the decay
    double releaseFramesPerDb = 0, releaseFactor = 1;
    double modReleaseFrames = 0; // for a full change
    Lfo modLfo, vibLfo;          // as each starts, with its delay
    double modLfoStep = 0, vibLfoStep = 0;
    bool pitchMoves = false; // an envelope or an LFO moves the pitch
  };

  struct Voice {
    bool active = false;
    bool held = false;
    bool looping = false;
    std::uint32_t plan = 0;
    std::uint64_t note = 0;    // its note-on's number in stream order
    std::uint64_t ordinal = 0; // which of its key's note-ons started it
    std::uint64_t noteOff = 0; // its note-off's number, once released
    std::uint8_t channel = 0, key = 0;
    // A taken voice's frames left to fade out, its next frame's gain being
    // fadeLeft over the fade's length; 0 for a voice that is not fading.
    std::uint32_t fadeLeft = 0;
    Envelope volume, modulation;
    double decayFactor = 1;    // the volume's fall per frame in its decay
    double modDecayFrames = 0; // the modulation decay's frames for a full change
    Lfo modLfo, vibLfo;
    // Cents from the zone's root, scaled and tuned, before modulation.
    double pitch = 0;
    double position = 0, increment = 0;
    // The gain of its total attenuation (initialAttenuation, the held
    // controllers and its velocity, clamped), times the filter's at DC.
    double gain = 1;
    // Cents: initialFilterFc plus the velocity modulator, before the
    // modulation envelope.
    double baseCutoff = 0;
    Filter filter;
  };

  // One place in the pool: its voice and, beside it, the last voice taken
  // from it while that one fades out. The tail does not count against the
  // voice limit.
  struct Slot {
    Voice voice, tail;
  };

  // One key's notes, a key being a channel and a key number. They are counted
  // whatever has become of their voices, so a note-off ends the note it
  // belongs to, the key's oldest held one, even when that note's voices have
  // already ended.
  struct KeyNotes {
    std::uint64_t noteOns = 0;  // its note-ons so far
    std::uint64_t noteOffs = 0; // its note-offs that found a held note
  };

  void adopt(SoundFont soundFont);
  void select(SoundFont::PresetNumber preset, std::vector<SoundFont::Zone> zones);
  void noteOn(std::uint8_t channel, std::uint8_t key, std::uint8_t velocity);
  void noteOff(std::uint8_t channel, std::uint8_t key);
  std::size_t takeNote();
  void scaleToKey(Voice& voice, const PreparedZone& plan, int key) const;
  void startRelease(Voice& voice);
  void render(Voice& voice, float* left, float* right, std::uint32_t from,
              std::uint32_t to);
  static bool sounds(Voice& voice, const PreparedZone& plan);
  static double volume(const Voice& voice, const PreparedZone& plan);
  static double modulation(const Voice& voice, const PreparedZone& plan);

  PortId midi_ = kNoPort, out_ = kNoPort;
  std::optional<SoundFont> sound_font_;
  SoundFont::PresetNumber preset_;
  std::vector<SoundFont::Zone> zones_;
  std::uint32_t voices_per_note_ = 0; // the most voices one note can start
  std::uint32_t voice_limit_ = kDefaultVoiceLimit;

  // Execution state, built by prepare().
  double rate_ = 0;
  std::uint32_t fade_frames_ = 1; // a taken voice's fade, 6 ms
  std::vector<PreparedZone> plans_;
  std::vector<Slot> slots_;    // the pool, one slot per voice of the limit
  std::vector<KeyNotes> keys_; // 16 channels of 128 keys
  std::uint64_t events_ = 0;   // note-ons and note-offs so far, which numbers each
};

} // namespace modular::nodes
