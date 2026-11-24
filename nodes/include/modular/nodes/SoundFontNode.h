#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
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
// fallen 96 dB after its attack or its sample has run out.
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
  enum class Stage : std::uint8_t { kDelay, kAttack, kHold, kDecay, kSustain, kRelease };

  // One zone of the selected preset, with what depends on the sample rate.
  struct PreparedZone {
    SoundFont::Zone zone;
    int root = 60;
    double rateRatio = 1;  // sample rate over graph rate
    double gainLeft = 0, gainRight = 0;
    std::uint64_t delayFrames = 0, attackFrames = 0, holdFrames = 0;
    double decayFactor = 1, sustainLevel = 1;
    std::uint64_t decayFrames = 0; // peak to sustain, or to the 96 dB floor
    bool decayEnds = false;        // the sustain lies at or below the floor
    double releaseFramesPerDb = 0;
    double releaseFactor = 1;
  };

  struct Voice {
    bool active = false;
    bool held = false;
    std::uint32_t plan = 0;
    std::uint64_t note = 0; // its note-on's number in stream order
    std::uint8_t channel = 0, key = 0;
    Stage stage = Stage::kDelay;
    std::uint64_t frame = 0; // frames into the stage
    std::uint64_t releaseFrames = 0;
    double level = 0;        // the envelope's level for the next frame
    double position = 0, increment = 0;
  };

  void adopt(SoundFont soundFont);
  void select(SoundFont::PresetNumber preset, std::vector<SoundFont::Zone> zones);
  void noteOn(std::uint8_t channel, std::uint8_t key, std::uint8_t velocity);
  void noteOff(std::uint8_t channel, std::uint8_t key);
  void startRelease(Voice& voice);
  void render(Voice& voice, float* left, float* right, std::uint32_t from,
              std::uint32_t to);
  static bool settle(Voice& voice, const PreparedZone& plan);
  static bool sounds(Voice& voice, const PreparedZone& plan);
  static double level(const Voice& voice, const PreparedZone& plan);

  PortId midi_ = kNoPort, out_ = kNoPort;
  std::optional<SoundFont> sound_font_;
  SoundFont::PresetNumber preset_;
  std::vector<SoundFont::Zone> zones_;
  std::uint32_t voices_per_note_ = 0; // the most voices one note can start
  std::uint32_t voice_limit_ = kDefaultVoiceLimit;

  // Execution state, built by prepare().
  std::vector<PreparedZone> plans_;
  std::vector<Voice> voices_;
  std::uint64_t note_ons_ = 0; // note-ons started, which numbers each note
};

} // namespace modular::nodes
