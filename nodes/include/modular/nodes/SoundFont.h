#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace modular::nodes {

// A SoundFont 2 file, read and checked whole at authoring time, as MidiFile
// reads MIDI: parsing never happens in prepare() or process(). It keeps the
// file's bytes as loaded, sample data included, so a zone plays from them
// directly. Errors throw std::invalid_argument("soundfont: ...").
class SoundFont {
public:
  // Generator numbers from the SoundFont 2.04 specification, section 8.1.2.
  enum Generator : std::uint16_t {
    kStartAddrsOffset = 0,
    kEndAddrsOffset = 1,
    kStartloopAddrsOffset = 2,
    kEndloopAddrsOffset = 3,
    kStartAddrsCoarseOffset = 4,
    kInitialFilterFc = 8,
    kModEnvToFilterFc = 11,
    kEndAddrsCoarseOffset = 12,
    kPan = 17,
    kDelayModEnv = 25,
    kAttackModEnv = 26,
    kHoldModEnv = 27,
    kDecayModEnv = 28,
    kSustainModEnv = 29,
    kReleaseModEnv = 30,
    kDelayVolEnv = 33,
    kAttackVolEnv = 34,
    kHoldVolEnv = 35,
    kDecayVolEnv = 36,
    kSustainVolEnv = 37,
    kReleaseVolEnv = 38,
    kInstrument = 41,
    kKeyRange = 43,
    kVelRange = 44,
    kStartloopAddrsCoarseOffset = 45,
    kInitialAttenuation = 48,
    kEndloopAddrsCoarseOffset = 50,
    kSampleId = 53,
    kSampleModes = 54,
    kOverridingRootKey = 58,
    kGeneratorCount = 61,
  };

  struct PresetNumber {
    std::uint16_t bank = 0;
    std::uint16_t program = 0;
    bool operator==(const PresetNumber&) const = default;
  };

  // One instrument zone as a preset plays it. Generators hold the final values:
  // instrument defaults, then its global and local zone, plus the preset's
  // global and local offsets, clamped to the specification's ranges.
  //
  // Its points, indexes into the sample data, are the sample header's plus the
  // zone's address offsets: the fine offset plus 32768 times the coarse one.
  // The start and end are kept within the header's sample.
  struct Zone {
    std::uint8_t keyLow = 0, keyHigh = 127;
    std::uint8_t velocityLow = 0, velocityHigh = 127;
    std::array<std::int32_t, kGeneratorCount> generators{};
    std::uint32_t sampleStart = 0; // the first point played
    std::uint32_t sampleEnd = 0;   // one past the last point played
    // The loop: its first point, and the point after its last. Empty when the
    // header's loop with its offsets is empty or lies outside the points the
    // zone plays, so the zone plays unlooped whatever its sampleModes.
    std::uint32_t loopStart = 0, loopEnd = 0;
    std::uint32_t sampleRate = 0;
    std::uint8_t originalPitch = 60;
  };

  static SoundFont parse(std::span<const std::uint8_t> bytes);
  static SoundFont read(const std::filesystem::path& path);

  // The lowest bank, then the lowest program in it.
  PresetNumber lowestPreset() const;
  // Every instrument zone the preset can play. Throws if the file lacks it.
  std::vector<Zone> zones(PresetNumber preset) const;

  // The file's bytes as loaded, whole: what a soundfont node saves.
  std::span<const std::uint8_t> bytes() const { return bytes_; }

  // One 16-bit point of the sample data.
  std::int16_t point(std::size_t index) const {
    const auto* p = bytes_.data() + sample_data_ + 2 * index;
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(p[0] | (p[1] << 8)));
  }

private:
  struct ParsedZone {
    std::array<std::int16_t, kGeneratorCount> amounts{};
    std::array<bool, kGeneratorCount> given{};
    std::uint16_t target = 0; // instrument or sample index
  };
  // A preset's or instrument's zones, and its global zone if it has one.
  struct Zones {
    std::optional<ParsedZone> global;
    std::vector<ParsedZone> local;
  };
  struct Preset {
    PresetNumber number;
    Zones zones;
  };
  struct Sample {
    std::uint32_t start = 0, end = 0, loopStart = 0, loopEnd = 0, rate = 0;
    std::uint8_t originalPitch = 60;
    bool rom = false; // its points are in a ROM, not in the file
  };

  std::vector<std::uint8_t> bytes_;
  std::size_t sample_data_ = 0; // byte offset of the first point
  std::vector<Preset> presets_;
  std::vector<Zones> instruments_;
  std::vector<Sample> samples_;
};

} // namespace modular::nodes
