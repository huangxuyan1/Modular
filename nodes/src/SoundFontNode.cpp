#include "modular/nodes/SoundFontNode.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace modular::nodes {
namespace {

constexpr double kPi = 3.14159265358979323846;
// A voice has ended once its envelope has fallen this far below its peak.
constexpr double kFloorDb = 96;

double seconds(std::int32_t timecents) { return std::exp2(timecents / 1200.0); }

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
  const auto preset = soundFont.lowestPreset();
  sound_font_ = std::move(soundFont);
  select(preset);
}

void SoundFontNode::load(std::span<const std::uint8_t>) {}
void SoundFontNode::load(const std::filesystem::path&) {}
void SoundFontNode::setPreset(std::uint16_t, std::uint16_t) {}
void SoundFontNode::setVoiceLimit(std::uint32_t) {}

void SoundFontNode::select(SoundFont::PresetNumber preset) {
  zones_ = sound_font_->zones(preset);
  preset_ = preset;
}

void SoundFontNode::prepare(const PrepareInfo& info) {
  if (!ready()) throw std::invalid_argument("soundfont: load a soundfont first");
  const double rate = info.sampleRate;
  if (!std::isfinite(rate) || rate <= 0)
    throw std::invalid_argument("soundfont: invalid sample rate");
  const auto frames = [&](std::int32_t timecents) {
    return static_cast<std::uint64_t>(std::llround(seconds(timecents) * rate));
  };
  plans_.clear();
  plans_.reserve(zones_.size());
  for (const auto& zone : zones_) {
    const auto& g = zone.generators;
    Plan plan;
    plan.zone = zone;
    plan.root = g[SoundFont::kOverridingRootKey] >= 0 ? g[SoundFont::kOverridingRootKey]
              : zone.originalPitch <= 127 ? zone.originalPitch : 60;
    plan.rateRatio = zone.sampleRate / rate;
    // Equal-power pan: sin on both sides keeps +-500 exactly one-sided.
    const double gain = std::pow(10.0, -g[SoundFont::kInitialAttenuation] / 200.0);
    const double pan = g[SoundFont::kPan];
    plan.gainLeft = gain * std::sin((500 - pan) / 1000 * kPi / 2);
    plan.gainRight = gain * std::sin((500 + pan) / 1000 * kPi / 2);
    plan.delayFrames = frames(g[SoundFont::kDelayVolEnv]);
    plan.attackFrames = frames(g[SoundFont::kAttackVolEnv]);
    plan.holdFrames = frames(g[SoundFont::kHoldVolEnv]);
    // Decay and release fall at a constant rate in dB: 100 dB over the time.
    const double decayFramesPerDb = seconds(g[SoundFont::kDecayVolEnv]) * rate / 100;
    plan.decayFactor = std::pow(10.0, -1 / (20 * decayFramesPerDb));
    const double sustainDb = g[SoundFont::kSustainVolEnv] / 10.0;
    plan.sustainLevel = std::pow(10.0, -sustainDb / 20);
    plan.decayEnds = sustainDb >= kFloorDb;
    plan.decayFrames = static_cast<std::uint64_t>(
        std::ceil(std::min(sustainDb, kFloorDb) * decayFramesPerDb));
    plan.releaseFramesPerDb = seconds(g[SoundFont::kReleaseVolEnv]) * rate / 100;
    plan.releaseFactor = std::pow(10.0, -1 / (20 * plan.releaseFramesPerDb));
    plans_.push_back(plan);
  }
  voices_.assign(voice_limit_, Voice{});
  notes_ = 0;
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
  if (needed == 0) return;
  ++notes_;
  std::size_t slot = 0;
  for (std::uint32_t p = 0; p < plans_.size(); ++p) {
    if (!matches(plans_[p].zone, key, velocity)) continue;
    while (slot < voices_.size() && voices_[slot].active) ++slot;
    if (slot == voices_.size()) return;
    auto& voice = voices_[slot];
    voice = Voice{};
    voice.active = voice.held = true;
    voice.plan = p;
    voice.note = notes_;
    voice.channel = channel;
    voice.key = key;
    voice.increment = std::exp2((key - plans_[p].root) / 12.0) * plans_[p].rateRatio;
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
  if (!settle(voice, plan)) {
    voice.active = false;
    return;
  }
  const double start = level(voice, plan);
  const double startDb = start > 0 ? -20 * std::log10(start) : kFloorDb;
  voice.stage = Stage::kRelease;
  voice.frame = 0;
  voice.level = start;
  voice.releaseFrames = startDb < kFloorDb ? static_cast<std::uint64_t>(std::ceil(
                            (kFloorDb - startDb) * plan.releaseFramesPerDb))
                                           : 0;
  if (voice.releaseFrames == 0) voice.active = false;
}

// Passes over the envelope's finished stages, so the voice's stage holds its
// next frame. False once its envelope has ended.
bool SoundFontNode::settle(Voice& voice, const Plan& plan) {
  for (;;) {
    switch (voice.stage) {
    case Stage::kDelay:
      if (voice.frame < plan.delayFrames) return true;
      voice.stage = Stage::kAttack;
      break;
    case Stage::kAttack:
      if (voice.frame < plan.attackFrames) return true;
      voice.stage = Stage::kHold;
      break;
    case Stage::kHold:
      if (voice.frame < plan.holdFrames) return true;
      voice.stage = Stage::kDecay;
      voice.level = 1;
      break;
    case Stage::kDecay:
      if (voice.frame < plan.decayFrames) return true;
      if (plan.decayEnds) return false;
      voice.stage = Stage::kSustain;
      voice.level = plan.sustainLevel;
      return true;
    case Stage::kSustain:
      return true;
    case Stage::kRelease:
      return voice.frame < voice.releaseFrames;
    }
    voice.frame = 0;
  }
}

// The envelope's level for the voice's next frame, once settled.
double SoundFontNode::level(const Voice& voice, const Plan& plan) {
  switch (voice.stage) {
  case Stage::kDelay:
    return 0;
  case Stage::kAttack: // a linear rise in amplitude, reaching the peak on its last frame
    return static_cast<double>(voice.frame + 1) / static_cast<double>(plan.attackFrames);
  case Stage::kHold:
    return 1;
  default:
    return voice.level;
  }
}

void SoundFontNode::render(Voice& voice, float* left, float* right, std::uint32_t from,
                           std::uint32_t to) {
  const auto& plan = plans_[voice.plan];
  const auto& zone = plan.zone;
  const std::uint64_t points = zone.sampleEnd - zone.sampleStart;
  for (std::uint32_t i = from; i < to; ++i) {
    if (!settle(voice, plan)) {
      voice.active = false;
      return;
    }
    if (voice.stage == Stage::kDelay) { // silent, and the sample waits
      ++voice.frame;
      continue;
    }
    const auto index = static_cast<std::uint64_t>(voice.position);
    if (index >= points) {
      voice.active = false;
      return;
    }
    // Linear interpolation; past the last point the sample is silent.
    const double a = sound_font_->point(zone.sampleStart + index);
    const double b = index + 1 < points ? sound_font_->point(zone.sampleStart + index + 1) : 0.0;
    const double sample =
        (a + (b - a) * (voice.position - index)) / 32768.0 * level(voice, plan);
    left[i] += static_cast<float>(sample * plan.gainLeft);
    right[i] += static_cast<float>(sample * plan.gainRight);
    voice.position += voice.increment;
    ++voice.frame;
    if (voice.stage == Stage::kDecay) voice.level *= plan.decayFactor;
    else if (voice.stage == Stage::kRelease) voice.level *= plan.releaseFactor;
  }
}

} // namespace modular::nodes
