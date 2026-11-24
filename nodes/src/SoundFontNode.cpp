#include "modular/nodes/SoundFontNode.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <stdexcept>

#include "SoundFontMaths.h"

namespace modular::nodes {
namespace {

namespace maths = soundfont_maths;

constexpr double kPi = std::numbers::pi;
// A voice has ended once its envelope has fallen this far below its peak.
constexpr double kFloorDb = 96;
// A voice taken for a new note, or ended by its exclusive class, ramps
// linearly to zero over this long, SFZ's off_mode=fast, so ending it does not
// click (map ticket 06, D5; map ticket 09, D8).
constexpr double kFadeSeconds = 0.006;

using Modulator = SoundFont::Modulator;

// The specification's default modulators (section 8.4) that move what the
// node plays, as map ticket 09's D2 reads them. Velocity to cutoff has no
// velocity switch, so a softer note is never brighter. The node follows no
// controller, so each reads its controller at General MIDI's power-on value
// (see mapped()): only CC7's adds anything, a constant 4.2 dB. The defaults to
// the effects sends are left out, since the node has none, and so is the
// pitch wheel's, whose destination is the pitch itself, no generator, and
// which adds nothing with the wheel centred. CC7 comes before velocity, so a
// note's attenuation sums in ticket 02's order, to the same bits.
constexpr Modulator kDefaultModulators[] = {
    {0x0587, SoundFont::kInitialAttenuation, 960, 0, 0}, // CC7, channel volume
    {0x0502, SoundFont::kInitialAttenuation, 960, 0, 0}, // note-on velocity
    {0x0102, SoundFont::kInitialFilterFc, -2400, 0, 0},  // note-on velocity
    {0x000d, SoundFont::kVibLfoToPitch, 50, 0, 0},       // channel pressure
    {0x0081, SoundFont::kVibLfoToPitch, 50, 0, 0},       // CC1, the modulation wheel
    {0x028a, SoundFont::kPan, 1000, 0, 0},               // CC10, pan
    {0x058b, SoundFont::kInitialAttenuation, 960, 0, 0}, // CC11, expression
};

// A file's instrument modulator replaces a default one with its source,
// destination and transform, whatever their amount sources. The leniency is
// map ticket 09's D2: it lets FluidR3's amount-0 modulators, whose amount
// source is a velocity switch, switch the default velocity-to-cutoff off.
bool replaces(const Modulator& modulator, const Modulator& fallback) {
  return modulator.source == fallback.source &&
         modulator.destination == fallback.destination &&
         modulator.transform == fallback.transform;
}

// A source's value on its curve, as a fraction. A modulator divides its amount
// by the denominators before it multiplies by the numerators, so the default
// modulators come out exactly as ticket 02 computed them: -400 log10(v / 127)
// centibels and -2400 (128 - v) / 128 cents.
struct Fraction {
  double numerator = 1, denominator = 1;
};

// How far a level of `fraction` lies below full, as centibels of the curves'
// 960: the specification's concave curve at 1 - fraction, and its convex
// curve's complement at fraction. Each curve ends at 1, so it is capped there.
double belowFull(double fraction) {
  return fraction > 0 ? std::min(960.0, -400 * maths::log10(fraction)) : 960;
}

// A source's value for a note (section 8.2.1). No controller is the constant
// 1. The note's velocity and key are the zone's, and every controller is held
// at General MIDI's power-on value: CC7 at 100, CC10 at 64, CC11 at 127, the
// pitch wheel centred and its sensitivity 2 semitones, every other one 0.
//
// A linear or switch source is normalized by its range, 128 values or the
// pitch wheel's 16384, as FluidSynth and ticket 02's velocity to cutoff do, so
// a centred controller is exactly half. A unipolar concave or convex curve
// runs over the controller's values from 0 to its top, 127, so the top gives
// 1, as ticket 02's velocity to attenuation does. A bipolar one runs from the
// centre out each way, its distance from the centre over half the range.
Fraction mapped(std::uint16_t source, int key, int velocity) {
  const int index = source & 0x7f;
  const bool controller = (source & 0x80) != 0;
  if (!controller && index == 0) return {};
  int value = 0, range = 128;
  if (controller) value = index == 7 ? 100 : index == 10 ? 64 : index == 11 ? 127 : 0;
  else if (index == 2) value = velocity;
  else if (index == 3) value = key;
  else if (index == 16) value = 2; // the pitch wheel's sensitivity
  else if (index == 14) {          // the pitch wheel
    value = 8192;
    range = 16384;
  }
  const bool down = (source & 0x100) != 0, bipolar = (source & 0x200) != 0;
  const auto fraction = [](int numerator, int denominator) {
    return Fraction{static_cast<double>(numerator), static_cast<double>(denominator)};
  };
  const int type = source >> 10;
  if (type == 0) { // linear
    if (bipolar) return fraction(down ? range - 2 * value : 2 * value - range, range);
    return fraction(down ? range - value : value, range);
  }
  if (type == 3) { // switch, on from half the range
    const bool on = (2 * value >= range) != down;
    return {on ? 1.0 : bipolar ? -1.0 : 0.0, 1};
  }
  const bool convex = type == 2; // else concave
  if (!bipolar) {
    const int top = range - 1;
    const int position = down ? top - value : value;
    return {convex ? 960 - belowFull(static_cast<double>(position) / top)
                   : belowFull(static_cast<double>(top - position) / top),
            960};
  }
  // Twice the distance from the centre, so over the range it is the distance
  // over half the range.
  const int distance = std::abs(2 * value - range);
  const double centibels =
      convex ? 960 - belowFull(static_cast<double>(distance) / range)
             : belowFull(static_cast<double>(range - distance) / range);
  const bool upper = (2 * value > range) != down;
  return {upper ? centibels : -centibels, 960};
}

// A modulator's output for a note: its amount times its source and its
// amount source, each on its curve, through its transform (0 linear, 2 the
// absolute value).
double output(const Modulator& modulator, int key, int velocity) {
  const auto source = mapped(modulator.source, key, velocity);
  const auto amount = mapped(modulator.amountSource, key, velocity);
  const double result = modulator.amount / (source.denominator * amount.denominator) *
                        source.numerator * amount.numerator;
  return modulator.transform == 2 ? std::abs(result) : result;
}

double seconds(double timecents) { return maths::exp2(timecents / 1200.0); }

std::uint64_t frames(double timecents, double rate) {
  return static_cast<std::uint64_t>(std::llround(seconds(timecents) * rate));
}

// Absolute cents, in which 6900 is 440 Hz, as hertz.
double hertz(double cents) { return 440 * maths::exp2((cents - 6900) / 1200); }

bool matches(const SoundFont::Zone& zone, std::uint8_t key, std::uint8_t velocity) {
  return key >= zone.keyLow && key <= zone.keyHigh && velocity >= zone.velocityLow &&
         velocity <= zone.velocityHigh;
}

// The keys whose notes the node counts: 16 channels of 128 keys.
constexpr std::size_t kKeysPerChannel = 128;
constexpr std::size_t kKeys = 16 * kKeysPerChannel;

// A key's place among the counts of its notes. The key is a MIDI data byte,
// below 128.
std::size_t keyIndex(std::uint8_t channel, std::uint8_t key) {
  return channel * kKeysPerChannel + key;
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
  rate_ = rate;
  fade_frames_ = static_cast<std::uint32_t>(
      std::max<long long>(1, std::llround(kFadeSeconds * rate)));
  plans_.clear();
  plans_.reserve(zones_.size());
  for (const auto& zone : zones_) {
    PreparedZone plan;
    plan.zone = zone;
    plan.rateRatio = zone.sampleRate / rate;
    // The default modulators no instrument modulator replaces come first, so
    // the default ones sum as ticket 02's did, then the file's.
    for (const auto& fallback : kDefaultModulators)
      if (std::none_of(zone.instrumentModulators.begin(), zone.instrumentModulators.end(),
                       [&](const Modulator& m) { return replaces(m, fallback); }))
        plan.modulators.push_back(fallback);
    plan.modulators.insert(plan.modulators.end(), zone.instrumentModulators.begin(),
                           zone.instrumentModulators.end());
    plan.modulators.insert(plan.modulators.end(), zone.presetModulators.begin(),
                           zone.presetModulators.end());
    plans_.push_back(std::move(plan));
  }
  slots_.assign(voice_limit_, Slot{});
  starting_.assign(voices_per_note_, Voice{});
  keys_.assign(kKeys, KeyNotes{});
  events_ = 0;
}

void SoundFontNode::process(ProcessContext& ctx) {
  const auto out = ctx.audioOut(out_);
  float* left = out.channels[0];
  float* right = out.channels[1];
  std::fill(left, left + ctx.numFrames(), 0.0f);
  std::fill(right, right + ctx.numFrames(), 0.0f);
  // Voices render between events in slot order, each slot's voice before its
  // tail, so every frame sums the same voices in the same order at any block
  // size.
  std::uint32_t done = 0;
  const auto renderTo = [&](std::uint32_t end) {
    for (auto& slot : slots_) {
      if (slot.voice.active) render(slot.voice, left, right, done, end);
      if (slot.tail.active) render(slot.tail, left, right, done, end);
    }
    done = end;
  };
  for (const Event& event : ctx.eventsIn(midi_)) {
    renderTo(event.offset);
    const auto message = event.as<MidiMsg>();
    // A data byte of 128 or more is not MIDI, and a key byte that large would
    // fall outside the counts of the keys' notes.
    if (!message.isValidChannelMessage()) continue;
    const auto channel = static_cast<std::uint8_t>(message.status & 0x0f);
    if (message.isNoteOn()) noteOn(channel, message.data1, message.data2);
    else if (message.isNoteOff()) noteOff(channel, message.data1);
  }
  renderTo(ctx.numFrames());
}

void SoundFontNode::noteOn(std::uint8_t channel, std::uint8_t key, std::uint8_t velocity) {
  // Every note-on is a held note of its key, even one that starts no voice.
  const auto number = ++events_;
  const auto ordinal = keys_[keyIndex(channel, key)].noteOns++;
  // The note's voices, one for each zone it matches, made before any takes a
  // slot. prepare() made room for the most one note starts.
  std::size_t needed = 0;
  for (const auto& plan : plans_) {
    if (!matches(plan.zone, key, velocity)) continue;
    auto& voice = starting_[needed++];
    start(voice, plan, key, velocity);
    voice.note = number;
    voice.ordinal = ordinal;
    voice.channel = channel;
    voice.key = key;
  }
  if (needed == 0) return;
  // Its exclusive classes end every other sounding voice of theirs, on any
  // channel, which frees their slots first.
  for (std::size_t i = 0; i < needed; ++i)
    if (starting_[i].exclusiveClass != 0) endClass(starting_[i].exclusiveClass);
  // A full pool takes whole notes until this one has its voices. prepare()
  // made the pool at least as large as any one note.
  auto idle = static_cast<std::size_t>(std::count_if(
      slots_.begin(), slots_.end(), [](const Slot& slot) { return !slot.voice.active; }));
  while (idle < needed) idle += takeNote();
  // A voice that ends before its first frame takes no slot.
  std::size_t slot = 0;
  for (std::size_t i = 0; i < needed; ++i) {
    if (!starting_[i].active) continue;
    while (slots_[slot].voice.active) ++slot;
    slots_[slot].voice = starting_[i];
  }
}

// Makes a voice of a zone for a note. Its generators are the zone's plus each
// modulator's output for the note, evaluated once, so every modulator is a
// fixed offset for the voice's life; from them it derives what it plays at the
// graph's rate.
void SoundFontNode::start(Voice& voice, const PreparedZone& plan, std::uint8_t key,
                          std::uint8_t velocity) const {
  const auto& zone = plan.zone;
  // Within the zone, keynum and velocity, when set, replace the note's own,
  // which is what the modulators read.
  const int zoneKey = zone.generators[SoundFont::kKeynum] >= 0
                          ? zone.generators[SoundFont::kKeynum] : key;
  const int zoneVelocity = zone.generators[SoundFont::kVelocity] >= 0
                               ? zone.generators[SoundFont::kVelocity] : velocity;
  SoundFont::Values g{};
  std::copy(zone.generators.begin(), zone.generators.end(), g.begin());
  for (const auto& modulator : plan.modulators)
    g[modulator.destination] += output(modulator, zoneKey, zoneVelocity);
  // A modulated value is kept within its generator's range, and whole where
  // the generator counts whole things, as the reader keeps a generator. The
  // cutoff is not: its terms sum unclamped, as ticket 02 sums the velocity
  // modulator's, and the filter keeps the sum below 0.45 of the graph's rate.
  for (std::size_t i = 0; i < g.size(); ++i)
    if (i != SoundFont::kInitialFilterFc) g[i] = SoundFont::bounded(i, g[i]);

  voice = Voice{};
  voice.active = voice.held = true;
  voice.exclusiveClass = static_cast<std::uint8_t>(g[SoundFont::kExclusiveClass]);

  // sampleModes 1 loops for the voice's whole life, 3 while it is held; 0, 2
  // and any other value play to the sample's end, as does a zone whose loop
  // is empty or lies outside its points.
  const auto points = SoundFont::place(zone.header, g);
  voice.start = points.start;
  voice.points = points.end - points.start;
  const auto mode = g[SoundFont::kSampleModes];
  voice.looping = points.loopEnd > points.loopStart && (mode == 1 || mode == 3);
  voice.loopsInRelease = voice.looping && mode == 1;
  if (voice.looping) {
    voice.loopStart = points.loopStart - points.start;
    voice.loopEnd = points.loopEnd - points.start;
  }

  const double root = g[SoundFont::kOverridingRootKey] >= 0
                          ? g[SoundFont::kOverridingRootKey]
                          : zone.originalPitch <= 127 ? zone.originalPitch : 60;
  const double tuning = 100 * g[SoundFont::kCoarseTune] + g[SoundFont::kFineTune] +
                        zone.pitchCorrection;
  voice.pitch = g[SoundFont::kScaleTuning] * (zoneKey - root) + tuning;
  voice.rateRatio = plan.rateRatio;
  voice.increment = maths::exp2(voice.pitch / 1200) * voice.rateRatio;
  voice.modEnvToPitch = g[SoundFont::kModEnvToPitch];
  voice.modLfoToPitch = g[SoundFont::kModLfoToPitch];
  voice.vibLfoToPitch = g[SoundFont::kVibLfoToPitch];
  voice.pitchMoves =
      voice.modEnvToPitch != 0 || voice.modLfoToPitch != 0 || voice.vibLfoToPitch != 0;

  // Equal-power pan: sin on both sides keeps +-500 exactly one-sided.
  const double pan = g[SoundFont::kPan];
  voice.panLeft = maths::sin((500 - pan) / 1000 * kPi / 2);
  voice.panRight = maths::sin((500 + pan) / 1000 * kPi / 2);
  // initialFilterQ, in centibels, raises the gain at the cutoff above a
  // Butterworth response's -3 dB there, which Q 0 cB gives, as FluidSynth
  // reads "not resonant": Q is 10^(dB / 20) / sqrt(2). The gain at DC falls by
  // half as many decibels, as the specification says.
  const double resonanceDb = g[SoundFont::kInitialFilterQ] / 10.0;
  voice.damping = std::numbers::sqrt2 * maths::exp10(-resonanceDb / 20);
  voice.gain = maths::exp10(-g[SoundFont::kInitialAttenuation] / 200) *
               maths::exp10(-resonanceDb / 40);
  voice.baseCutoff = g[SoundFont::kInitialFilterFc];
  voice.modEnvToFilterFc = g[SoundFont::kModEnvToFilterFc];
  voice.modLfoToFilterFc = g[SoundFont::kModLfoToFilterFc];
  voice.modLfoToVolume = g[SoundFont::kModLfoToVolume];

  // Both envelopes. The volume's release falls at a constant rate in dB,
  // 100 dB over its time; the modulation's moves linearly, a full change
  // between 1 and 0 over its time. The modulation's sustain is a decrease in
  // 0.1 % steps.
  auto& volume = voice.volume.timeline;
  volume.delay = frames(g[SoundFont::kDelayVolEnv], rate_);
  volume.attack = frames(g[SoundFont::kAttackVolEnv], rate_);
  const double sustainDb = g[SoundFont::kSustainVolEnv] / 10.0;
  volume.sustain = maths::exp10(-sustainDb / 20);
  volume.decayEnds = sustainDb >= kFloorDb;
  voice.releaseFramesPerDb = seconds(g[SoundFont::kReleaseVolEnv]) * rate_ / 100;
  voice.releaseFactor = maths::exp10(-1 / (20 * voice.releaseFramesPerDb));
  auto& modulation = voice.modulation.timeline;
  modulation.delay = frames(g[SoundFont::kDelayModEnv], rate_);
  modulation.attack = frames(g[SoundFont::kAttackModEnv], rate_);
  modulation.sustain = 1 - g[SoundFont::kSustainModEnv] / 1000.0;
  voice.modReleaseFrames = seconds(g[SoundFont::kReleaseModEnv]) * rate_;
  scaleToKey(voice, g, zoneKey);

  voice.modLfo.delay = frames(g[SoundFont::kDelayModLfo], rate_);
  voice.modLfo.step = hertz(g[SoundFont::kFreqModLfo]) / rate_;
  voice.vibLfo.delay = frames(g[SoundFont::kDelayVibLfo], rate_);
  voice.vibLfo.step = hertz(g[SoundFont::kFreqVibLfo]) / rate_;
  voice.modulation.settle();
  voice.active = sounds(voice);
}

// Each envelope's hold and decay at a key: keynumTo...Hold and keynumTo...Decay
// take their timecents off the stage for every key above 60 and add them for
// every key below, so 100 halves the stage an octave up. The result stays
// within the stage's own range.
void SoundFontNode::scaleToKey(Voice& voice, const SoundFont::Values& g, int key) const {
  const auto atKey = [&](SoundFont::Generator stage, SoundFont::Generator perKey,
                         double longest) {
    return std::clamp(g[stage] + g[perKey] * (60 - key), -12000.0, longest);
  };
  auto& volume = voice.volume.timeline;
  volume.hold =
      frames(atKey(SoundFont::kHoldVolEnv, SoundFont::kKeynumToVolEnvHold, 5000), rate_);
  const double decayFramesPerDb =
      seconds(atKey(SoundFont::kDecayVolEnv, SoundFont::kKeynumToVolEnvDecay, 8000)) *
      rate_ / 100;
  voice.decayFactor = maths::exp10(-1 / (20 * decayFramesPerDb));
  const double sustainDb = g[SoundFont::kSustainVolEnv] / 10.0;
  volume.decay = static_cast<std::uint64_t>(
      std::ceil(std::min(sustainDb, kFloorDb) * decayFramesPerDb));

  auto& modulation = voice.modulation.timeline;
  modulation.hold = frames(
      atKey(SoundFont::kHoldModEnv, SoundFont::kKeynumToModEnvHold, 5000), rate_);
  voice.modDecayFrames =
      seconds(atKey(SoundFont::kDecayModEnv, SoundFont::kKeynumToModEnvDecay, 8000)) *
      rate_;
  const double sustainFall = g[SoundFont::kSustainModEnv] / 1000.0;
  modulation.decay =
      static_cast<std::uint64_t>(std::ceil(sustainFall * voice.modDecayFrames));
}

// Ends the key's oldest held note: it releases every voice of that note still
// sounding. If they have all ended, the note-off is spent on that note and
// releases no other. With no held note on the key it does nothing, and the
// node keeps no credit for a later note-on.
void SoundFontNode::noteOff(std::uint8_t channel, std::uint8_t key) {
  const auto number = ++events_;
  auto& notes = keys_[keyIndex(channel, key)];
  if (notes.noteOffs == notes.noteOns) return;
  const auto ordinal = notes.noteOffs++;
  for (auto& slot : slots_) {
    auto& voice = slot.voice;
    if (voice.active && voice.channel == channel && voice.key == key &&
        voice.ordinal == ordinal) {
      voice.noteOff = number;
      startRelease(voice);
    }
  }
}

// Takes the note a full pool gives up first: the released note whose note-off
// came first or, with none released, the held note whose note-on came first.
// Every voice of that note moves to its slot's tail to fade out, and an older
// tail there stops at once. Returns how many voices it frees.
std::size_t SoundFontNode::takeNote() {
  const auto before = [](const Voice& a, const Voice& b) {
    if (a.held != b.held) return !a.held;
    return a.held ? a.note < b.note : a.noteOff < b.noteOff;
  };
  const Voice* first = nullptr;
  for (const auto& slot : slots_)
    if (slot.voice.active && (first == nullptr || before(slot.voice, *first)))
      first = &slot.voice;
  const auto note = first->note;
  std::size_t taken = 0;
  for (auto& slot : slots_)
    if (slot.voice.active && slot.voice.note == note) {
      fadeOut(slot);
      ++taken;
    }
  return taken;
}

// Ends every sounding voice of an exclusive class, on every channel (map ticket
// 09, D8): each fades out as a taken voice does, and its slot is free at once.
void SoundFontNode::endClass(std::uint8_t exclusiveClass) {
  for (auto& slot : slots_)
    if (slot.voice.active && slot.voice.exclusiveClass == exclusiveClass) fadeOut(slot);
}

// Ends a slot's voice early: it moves to the slot's tail, which stops an older
// tail there at once, to fade out over 6 ms from this frame, and its slot is
// free.
void SoundFontNode::fadeOut(Slot& slot) const {
  slot.tail = slot.voice;
  slot.tail.fadeLeft = fade_frames_;
  slot.voice.active = false;
}

// The release falls from the envelope's current level at its own rate, so it
// ends sooner the lower it starts. A loop that lasts only while the note is
// held lets the voice play on through its end.
void SoundFontNode::startRelease(Voice& voice) {
  voice.held = false;
  voice.looping = voice.looping && voice.loopsInRelease;
  if (voice.modulation.settle()) {
    const double from = modulation(voice);
    voice.modulation.release(
        from, static_cast<std::uint64_t>(std::ceil(from * voice.modReleaseFrames)));
  }
  if (!voice.volume.settle()) {
    voice.active = false;
    return;
  }
  const double start = volume(voice);
  const double startDb = start > 0 ? -20 * maths::log10(start) : kFloorDb;
  voice.volume.release(start, startDb < kFloorDb
                                  ? static_cast<std::uint64_t>(std::ceil(
                                        (kFloorDb - startDb) * voice.releaseFramesPerDb))
                                  : 0);
  if (voice.volume.stage == Stage::kOff) voice.active = false;
}

bool SoundFontNode::Envelope::settle() {
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
double SoundFontNode::volume(const Voice& voice) {
  const auto& envelope = voice.volume;
  switch (envelope.stage) {
  case Stage::kDelay:
  case Stage::kOff:
    return 0;
  case Stage::kAttack: // a linear rise in amplitude, reaching the peak on its last frame
    return static_cast<double>(envelope.frame + 1) /
           static_cast<double>(envelope.timeline.attack);
  case Stage::kHold:
    return 1;
  default:
    return envelope.level;
  }
}

// The modulation envelope's level for the voice's next frame, once settled:
// 0 in its delay and once its release is over.
double SoundFontNode::modulation(const Voice& voice) {
  const auto& envelope = voice.modulation;
  const auto frame = static_cast<double>(envelope.frame);
  switch (envelope.stage) {
  case Stage::kAttack: // a linear rise, reaching 1 on its last frame
    return (frame + 1) / static_cast<double>(envelope.timeline.attack);
  case Stage::kHold:
    return 1;
  case Stage::kDecay:
    return 1 - frame / voice.modDecayFrames;
  case Stage::kSustain:
    return envelope.timeline.sustain;
  case Stage::kRelease:
    return envelope.level - frame / voice.modReleaseFrames;
  default:
    return 0;
  }
}

// The specification's triangle, rising from 0 when its delay is over.
double SoundFontNode::Lfo::value() const {
  if (delay > 0) return 0;
  if (phase < 0.25) return 4 * phase;
  if (phase < 0.75) return 2 - 4 * phase;
  return 4 * phase - 4;
}

void SoundFontNode::Lfo::advance() {
  if (delay > 0) {
    --delay;
    return;
  }
  phase += step;
  if (phase >= 1) phase -= 1;
}

// The specification's two-pole low-pass, as a state-variable filter whose
// response is the bilinear transform's: its cutoff is exact, it passes a
// steady signal unchanged, and its coefficients change smoothly with the
// cutoff. `damping` is 1 / Q. The cutoff is kept below 0.45 of the graph's
// rate, as FluidSynth keeps it, so a high one cannot reach Nyquist.
void SoundFontNode::Filter::tune(std::int64_t cutoff, double rate, double damping) {
  cents = cutoff;
  const double angle =
      kPi * std::min(hertz(static_cast<double>(cutoff)), 0.45 * rate) / rate;
  const double g = maths::sin(angle) / maths::cos(angle);
  a1 = 1 / (1 + g * (g + damping));
  a2 = g * a1;
  a3 = g * a2;
}

double SoundFontNode::Filter::step(double in) {
  const double v3 = in - state2;
  const double v1 = a1 * state1 + a2 * v3;
  const double v2 = state2 + a2 * state1 + a3 * v3;
  state1 = 2 * v1 - state1;
  state2 = 2 * v2 - state2;
  return v2;
}

// Settles a voice for its next frame. False once it has ended: a voice ends
// on the frame after its last sound, so a note-on there finds its slot free.
bool SoundFontNode::sounds(Voice& voice) {
  if (!voice.volume.settle()) return false;
  return static_cast<std::uint64_t>(voice.position) < voice.points;
}

void SoundFontNode::render(Voice& voice, float* left, float* right, std::uint32_t from,
                           std::uint32_t to) const {
  const auto start = voice.start;
  const auto loopLength = static_cast<double>(voice.loopEnd - voice.loopStart);
  for (std::uint32_t i = from; i < to; ++i) {
    auto& envelope = voice.volume;
    if (envelope.stage != Stage::kDelay) { // in the delay, silent, and the sample waits
      const double modEnv = modulation(voice);
      const double modLfo = voice.modLfo.value();
      // The cutoff in whole cents: the voice's base, plus the modulation
      // envelope's and the modulation LFO's terms.
      const auto cents = std::llround(voice.baseCutoff + voice.modEnvToFilterFc * modEnv +
                                      voice.modLfoToFilterFc * modLfo);
      if (cents != voice.filter.cents) voice.filter.tune(cents, rate_, voice.damping);
      // Linear interpolation. While looping, the loop's last point is followed
      // by its first, so no point past the loop is read; past the sample's
      // last point the sample is silent.
      const auto index = static_cast<std::uint64_t>(voice.position);
      auto next = index + 1;
      if (voice.looping && next == voice.loopEnd) next = voice.loopStart;
      const double a = sound_font_->point(start + index);
      const double b = next < voice.points ? sound_font_->point(start + next) : 0.0;
      // The filter comes before the amplifier, as in the specification's
      // synthesis model.
      double sample =
          voice.filter.step((a + (b - a) * (voice.position - index)) / 32768.0) *
          volume(voice) * voice.gain;
      // The modulation LFO's volume term, in centibels: a positive depth
      // makes the LFO's rise louder.
      if (voice.modLfoToVolume != 0)
        sample *= maths::exp10(voice.modLfoToVolume * modLfo / 200);
      // A taken voice falls in equal steps to zero, from full on the new
      // note's frame.
      if (voice.fadeLeft > 0)
        sample *= voice.fadeLeft / static_cast<double>(fade_frames_);
      left[i] += static_cast<float>(sample * voice.panLeft);
      right[i] += static_cast<float>(sample * voice.panRight);
      // The pitch's modulation, in cents: the modulation envelope's and both
      // LFOs' terms.
      if (voice.pitchMoves)
        voice.increment = maths::exp2((voice.pitch + voice.modEnvToPitch * modEnv +
                                       voice.modLfoToPitch * modLfo +
                                       voice.vibLfoToPitch * voice.vibLfo.value()) /
                                      1200) *
                          voice.rateRatio;
      voice.position += voice.increment;
      if (voice.looping && voice.position >= voice.loopEnd)
        voice.position =
            voice.loopStart + std::fmod(voice.position - voice.loopStart, loopLength);
      if (envelope.stage == Stage::kDecay) envelope.level *= voice.decayFactor;
      else if (envelope.stage == Stage::kRelease) envelope.level *= voice.releaseFactor;
    }
    ++envelope.frame;
    ++voice.modulation.frame;
    voice.modulation.settle();
    voice.modLfo.advance();
    voice.vibLfo.advance();
    // A taken voice ends with its fade, unless it ends by itself first.
    if ((voice.fadeLeft > 0 && --voice.fadeLeft == 0) || !sounds(voice)) {
      voice.active = false;
      return;
    }
  }
}

} // namespace modular::nodes
