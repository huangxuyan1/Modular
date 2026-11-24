#include "modular/nodes/SoundFontNode.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "SoundFontMaths.h"

namespace modular::nodes {
namespace {

namespace maths = soundfont_maths;

constexpr double kPi = 3.14159265358979323846;
// A voice has ended once its envelope has fallen this far below its peak.
constexpr double kFloorDb = 96;

double seconds(std::int32_t timecents) { return maths::exp2(timecents / 1200.0); }

bool matches(const SoundFont::Zone& zone, std::uint8_t key, std::uint8_t velocity) {
  return key >= zone.keyLow && key <= zone.keyHigh && velocity >= zone.velocityLow &&
         velocity <= zone.velocityHigh;
}

} // namespace

SoundFontNode::SoundFontNode(Graph& graph) : Node(graph) {
  midi_ = addInput(PortType::event(subtype::Midi), "midi");
  out_ = addOutput(PortType::audio(2), "out");
}

SoundFontNode::SoundFontNode(Graph& graph, SoundFont soundFont) : SoundFontNode(graph) {
  adopt(std::move(soundFont));
}

void SoundFontNode::load(std::span<const std::uint8_t> bytes) {
  adopt(SoundFont::parse(bytes));
}

void SoundFontNode::load(const std::filesystem::path& path) {
  adopt(SoundFont::read(path));
}

void SoundFontNode::adopt(SoundFont soundFont) {
  const auto preset = soundFont.lowestPreset();
  auto zones = soundFont.zones(preset);
  sound_font_ = std::move(soundFont);
  select(preset, std::move(zones));
}

void SoundFontNode::setPreset(std::uint16_t bank, std::uint16_t program) {
  if (!ready()) throw std::invalid_argument("soundfont: load a soundfont first");
  const SoundFont::PresetNumber preset{bank, program};
  select(preset, sound_font_->zones(preset));
}

void SoundFontNode::setVoiceLimit(std::uint32_t limit) {
  if (limit < 1 || limit > 65535)
    throw std::invalid_argument("soundfont: voice limit must be 1 to 65535");
  voice_limit_ = limit;
}

void SoundFontNode::select(SoundFont::PresetNumber preset,
                           std::vector<SoundFont::Zone> zones) {
  // The most zones any one key and velocity match: the voices one note starts.
  std::uint32_t most = 0;
  for (int key = 0; key < 128; ++key)
    for (int velocity = 1; velocity < 128; ++velocity) {
      std::uint32_t count = 0;
      for (const auto& zone : zones)
        count += matches(zone, static_cast<std::uint8_t>(key),
                         static_cast<std::uint8_t>(velocity)) ? 1 : 0;
      most = std::max(most, count);
    }
  preset_ = preset;
  zones_ = std::move(zones);
  voices_per_note_ = most;
}

void SoundFontNode::prepare(const PrepareInfo& info) {
  if (!ready()) throw std::invalid_argument("soundfont: load a soundfont first");
  const double rate = info.sampleRate;
  if (!std::isfinite(rate) || rate <= 0)
    throw std::invalid_argument("soundfont: invalid sample rate");
  if (voice_limit_ < voices_per_note_)
    throw std::invalid_argument(
        "soundfont: voice limit is below the voices one note starts");
  const auto frames = [&](std::int32_t timecents) {
    return static_cast<std::uint64_t>(std::llround(seconds(timecents) * rate));
  };
  rate_ = rate;
  plans_.clear();
  plans_.reserve(zones_.size());
  for (const auto& zone : zones_) {
    const auto& g = zone.generators;
    PreparedZone plan;
    plan.zone = zone;
    plan.root = g[SoundFont::kOverridingRootKey] >= 0 ? g[SoundFont::kOverridingRootKey]
              : zone.originalPitch <= 127 ? zone.originalPitch : 60;
    plan.rateRatio = zone.sampleRate / rate;
    // Equal-power pan: sin on both sides keeps +-500 exactly one-sided.
    const double pan = g[SoundFont::kPan];
    plan.panLeft = maths::sin((500 - pan) / 1000 * kPi / 2);
    plan.panRight = maths::sin((500 + pan) / 1000 * kPi / 2);
    plan.attenuation = g[SoundFont::kInitialAttenuation];
    plan.points = zone.sampleEnd - zone.sampleStart;

    plan.volume.delay = frames(g[SoundFont::kDelayVolEnv]);
    plan.volume.attack = frames(g[SoundFont::kAttackVolEnv]);
    plan.volume.hold = frames(g[SoundFont::kHoldVolEnv]);
    // Decay and release fall at a constant rate in dB: 100 dB over the time.
    const double decayFramesPerDb = seconds(g[SoundFont::kDecayVolEnv]) * rate / 100;
    plan.decayFactor = maths::exp10(-1 / (20 * decayFramesPerDb));
    const double sustainDb = g[SoundFont::kSustainVolEnv] / 10.0;
    plan.volume.sustain = maths::exp10(-sustainDb / 20);
    plan.volume.decayEnds = sustainDb >= kFloorDb;
    plan.volume.decay = static_cast<std::uint64_t>(
        std::ceil(std::min(sustainDb, kFloorDb) * decayFramesPerDb));
    plan.releaseFramesPerDb = seconds(g[SoundFont::kReleaseVolEnv]) * rate / 100;
    plan.releaseFactor = maths::exp10(-1 / (20 * plan.releaseFramesPerDb));
    plans_.push_back(plan);
  }
  voices_.assign(voice_limit_, Voice{});
  note_ons_ = 0;
}

void SoundFontNode::process(ProcessContext& ctx) {
  const auto out = ctx.audioOut(out_);
  float* left = out.channels[0];
  float* right = out.channels[1];
  std::fill(left, left + ctx.numFrames(), 0.0f);
  std::fill(right, right + ctx.numFrames(), 0.0f);
  // Voices render between events in slot order, so every frame sums the same
  // voices in the same order at any block size.
  std::uint32_t done = 0;
  const auto renderTo = [&](std::uint32_t end) {
    for (auto& voice : voices_)
      if (voice.active) render(voice, left, right, done, end);
    done = end;
  };
  for (const Event& event : ctx.eventsIn(midi_)) {
    renderTo(event.offset);
    const auto message = event.as<MidiMsg>();
    const auto channel = static_cast<std::uint8_t>(message.status & 0x0f);
    if (message.isNoteOn()) noteOn(channel, message.data1, message.data2);
    else if (message.isNoteOff()) noteOff(channel, message.data1);
  }
  renderTo(ctx.numFrames());
}

void SoundFontNode::noteOn(std::uint8_t channel, std::uint8_t key, std::uint8_t velocity) {
  std::size_t needed = 0;
  for (const auto& plan : plans_) needed += matches(plan.zone, key, velocity) ? 1 : 0;
  // Interim, until a full pool takes whole notes (ticket 03): a note that
  // finds too few free voices for all its zones does not start.
  const auto idle = std::count_if(voices_.begin(), voices_.end(),
                                  [](const Voice& voice) { return !voice.active; });
  if (needed == 0 || static_cast<std::size_t>(idle) < needed) return;
  ++note_ons_;
  std::size_t slot = 0;
  for (std::uint32_t p = 0; p < plans_.size(); ++p) {
    const auto& plan = plans_[p];
    if (!matches(plan.zone, key, velocity)) continue;
    while (voices_[slot].active) ++slot;
    auto& voice = voices_[slot];
    voice = Voice{};
    voice.active = voice.held = true;
    voice.plan = p;
    voice.note = note_ons_;
    voice.channel = channel;
    voice.key = key;
    voice.increment = maths::exp2((key - plan.root) / 12.0) * plan.rateRatio;
    voice.gain = maths::exp10(-plan.attenuation / 200);
    voice.active = sounds(voice, plan);
  }
}

// Ends the oldest held note of the key: every voice its note-on started.
void SoundFontNode::noteOff(std::uint8_t channel, std::uint8_t key) {
  auto oldest = std::numeric_limits<std::uint64_t>::max();
  for (const auto& voice : voices_)
    if (voice.active && voice.held && voice.channel == channel && voice.key == key)
      oldest = std::min(oldest, voice.note);
  for (auto& voice : voices_)
    if (voice.active && voice.held && voice.note == oldest) startRelease(voice);
}

// The release falls from the envelope's current level at its own rate, so it
// ends sooner the lower it starts.
void SoundFontNode::startRelease(Voice& voice) {
  voice.held = false;
  const auto& plan = plans_[voice.plan];
  if (!voice.volume.settle(plan.volume)) {
    voice.active = false;
    return;
  }
  const double start = volume(voice, plan);
  const double startDb = start > 0 ? -20 * maths::log10(start) : kFloorDb;
  voice.volume.release(start, startDb < kFloorDb
                                  ? static_cast<std::uint64_t>(std::ceil(
                                        (kFloorDb - startDb) * plan.releaseFramesPerDb))
                                  : 0);
  if (voice.volume.stage == Stage::kOff) voice.active = false;
}

bool SoundFontNode::Envelope::settle(const Timeline& timeline) {
  for (;;) {
    switch (stage) {
    case Stage::kDelay:
      if (frame < timeline.delay) return true;
      stage = Stage::kAttack;
      break;
    case Stage::kAttack:
      if (frame < timeline.attack) return true;
      stage = Stage::kHold;
      break;
    case Stage::kHold:
      if (frame < timeline.hold) return true;
      stage = Stage::kDecay;
      level = 1;
      break;
    case Stage::kDecay:
      if (frame < timeline.decay) return true;
      stage = timeline.decayEnds ? Stage::kOff : Stage::kSustain;
      level = timeline.sustain;
      break;
    case Stage::kSustain:
      return true;
    case Stage::kRelease:
      if (frame < releaseFrames) return true;
      stage = Stage::kOff;
      break;
    case Stage::kOff:
      return false;
    }
    frame = 0;
  }
}

void SoundFontNode::Envelope::release(double from, std::uint64_t frames) {
  stage = frames > 0 ? Stage::kRelease : Stage::kOff;
  frame = 0;
  level = from;
  releaseFrames = frames;
}

// The volume envelope's level for the voice's next frame, once settled.
double SoundFontNode::volume(const Voice& voice, const PreparedZone& plan) {
  const auto& envelope = voice.volume;
  switch (envelope.stage) {
  case Stage::kDelay:
  case Stage::kOff:
    return 0;
  case Stage::kAttack: // a linear rise in amplitude, reaching the peak on its last frame
    return static_cast<double>(envelope.frame + 1) / static_cast<double>(plan.volume.attack);
  case Stage::kHold:
    return 1;
  default:
    return envelope.level;
  }
}

// Settles a voice for its next frame. False once it has ended: a voice ends
// on the frame after its last sound, so a note-on there finds its slot free.
bool SoundFontNode::sounds(Voice& voice, const PreparedZone& plan) {
  if (!voice.volume.settle(plan.volume)) return false;
  return static_cast<std::uint64_t>(voice.position) < plan.points;
}

void SoundFontNode::render(Voice& voice, float* left, float* right, std::uint32_t from,
                           std::uint32_t to) {
  const auto& plan = plans_[voice.plan];
  const auto start = plan.zone.sampleStart;
  for (std::uint32_t i = from; i < to; ++i) {
    auto& envelope = voice.volume;
    if (envelope.stage != Stage::kDelay) { // in the delay, silent, and the sample waits
      // Linear interpolation; past the last point the sample is silent.
      const auto index = static_cast<std::uint64_t>(voice.position);
      const double a = sound_font_->point(start + index);
      const double b = index + 1 < plan.points ? sound_font_->point(start + index + 1) : 0.0;
      const double sample = (a + (b - a) * (voice.position - index)) / 32768.0 *
                            volume(voice, plan) * voice.gain;
      left[i] += static_cast<float>(sample * plan.panLeft);
      right[i] += static_cast<float>(sample * plan.panRight);
      voice.position += voice.increment;
      if (envelope.stage == Stage::kDecay) envelope.level *= plan.decayFactor;
      else if (envelope.stage == Stage::kRelease) envelope.level *= plan.releaseFactor;
    }
    ++envelope.frame;
    if (!sounds(voice, plan)) {
      voice.active = false;
      return;
    }
  }
}

} // namespace modular::nodes
