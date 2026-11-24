#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Writes small SoundFont 2 files in memory, so the soundfont tests need no
// licensed file, as the MIDI-file tests write Standard MIDI Files. It writes
// what a test describes and checks nothing: a test that wants a malformed file
// describes one, or edits the parts before assembling them.
//
// Layout and record sizes follow the SoundFont 2.04 specification: a RIFF
// 'sfbk' form holding the INFO, sdta and pdta lists, with every hydra list
// closed by its terminal record. Each sample is followed by 46 zero points.
namespace sf2test {

using Bytes = std::vector<std::uint8_t>;

// Generator numbers, from section 8.1.2 of the specification.
namespace gen {
inline constexpr std::uint16_t kStartAddrsOffset = 0;
inline constexpr std::uint16_t kEndAddrsOffset = 1;
inline constexpr std::uint16_t kStartloopAddrsOffset = 2;
inline constexpr std::uint16_t kEndloopAddrsOffset = 3;
inline constexpr std::uint16_t kStartAddrsCoarseOffset = 4;
inline constexpr std::uint16_t kInitialFilterFc = 8;
inline constexpr std::uint16_t kModEnvToFilterFc = 11;
inline constexpr std::uint16_t kEndAddrsCoarseOffset = 12;
inline constexpr std::uint16_t kReverbEffectsSend = 16;
inline constexpr std::uint16_t kPan = 17;
inline constexpr std::uint16_t kDelayModEnv = 25;
inline constexpr std::uint16_t kAttackModEnv = 26;
inline constexpr std::uint16_t kHoldModEnv = 27;
inline constexpr std::uint16_t kDecayModEnv = 28;
inline constexpr std::uint16_t kSustainModEnv = 29;
inline constexpr std::uint16_t kReleaseModEnv = 30;
inline constexpr std::uint16_t kDelayVolEnv = 33;
inline constexpr std::uint16_t kAttackVolEnv = 34;
inline constexpr std::uint16_t kHoldVolEnv = 35;
inline constexpr std::uint16_t kDecayVolEnv = 36;
inline constexpr std::uint16_t kSustainVolEnv = 37;
inline constexpr std::uint16_t kReleaseVolEnv = 38;
inline constexpr std::uint16_t kInstrument = 41;
inline constexpr std::uint16_t kKeyRange = 43;
inline constexpr std::uint16_t kVelRange = 44;
inline constexpr std::uint16_t kStartloopAddrsCoarseOffset = 45;
inline constexpr std::uint16_t kInitialAttenuation = 48;
inline constexpr std::uint16_t kEndloopAddrsCoarseOffset = 50;
inline constexpr std::uint16_t kCoarseTune = 51;
inline constexpr std::uint16_t kSampleId = 53;
inline constexpr std::uint16_t kSampleModes = 54;
inline constexpr std::uint16_t kOverridingRootKey = 58;
} // namespace gen

// Sample types, from section 7.10.
inline constexpr std::uint16_t kMonoSample = 1;
inline constexpr std::uint16_t kCompressedSample = 0x10;
inline constexpr std::uint16_t kRomSample = 0x8000;

struct Generator {
  std::uint16_t oper;
  std::uint16_t amount;
};

inline Generator set(std::uint16_t oper, int amount) {
  return {oper, static_cast<std::uint16_t>(static_cast<std::int16_t>(amount))};
}

inline Generator range(std::uint16_t oper, int low, int high) {
  return {oper, static_cast<std::uint16_t>(low | (high << 8))};
}

struct Modulator {
  std::uint16_t source = 0;
  std::uint16_t destination = 0;
  std::int16_t amount = 0;
  std::uint16_t amountSource = 0;
  std::uint16_t transform = 0;
};

// A zone's generators are written in the order given, the terminal one
// (instrument or sampleID) included, so a test can write a global zone, a
// misplaced generator or an index out of range.
struct Zone {
  std::vector<Generator> generators;
  std::vector<Modulator> modulators;
};

struct Preset {
  std::string name = "preset";
  std::uint16_t program = 0;
  std::uint16_t bank = 0;
  std::vector<Zone> zones;
};

struct Instrument {
  std::string name = "instrument";
  std::vector<Zone> zones;
};

// A loop's first point and the point after its last, counted from the
// sample's first point. Either may lie outside the sample, for a bad loop.
struct Loop {
  std::int64_t start = 0;
  std::int64_t end = 0;
};

struct Sample {
  std::string name = "sample";
  std::vector<std::int16_t> points;
  std::uint32_t rate = 48000;
  std::uint8_t originalPitch = 60;
  std::uint16_t type = kMonoSample;
  std::optional<Loop> loop; // without one, the header's loop is the whole sample
};

struct File {
  std::uint16_t major = 2;
  std::uint16_t minor = 4;
  std::vector<Preset> presets;
  std::vector<Instrument> instruments;
  std::vector<Sample> samples;
  bool sm24 = false; // adds a 24-bit extension chunk of zeros
};

struct Chunk {
  std::string tag;
  Bytes data;
};

// The chunks of the three lists, before they are wrapped.
struct Parts {
  std::vector<Chunk> info;
  std::vector<Chunk> sdta;
  std::vector<Chunk> pdta; // phdr, pbag, pmod, pgen, inst, ibag, imod, igen, shdr
};

namespace detail {

inline void le(Bytes& out, std::uint32_t value, int size) {
  for (int i = 0; i < size; ++i) out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

inline void name(Bytes& out, const std::string& text) {
  for (std::size_t i = 0; i < 20; ++i)
    out.push_back(i < text.size() ? static_cast<std::uint8_t>(text[i]) : 0);
}

inline void chunk(Bytes& out, const std::string& tag, const Bytes& data) {
  out.insert(out.end(), tag.begin(), tag.end());
  le(out, static_cast<std::uint32_t>(data.size()), 4);
  out.insert(out.end(), data.begin(), data.end());
  if (data.size() % 2 != 0) out.push_back(0);
}

inline Bytes list(const std::string& type, const std::vector<Chunk>& chunks) {
  Bytes data(type.begin(), type.end());
  for (const auto& c : chunks) chunk(data, c.tag, c.data);
  return data;
}

struct ZoneLists {
  Bytes bags, mods, gens;
  std::uint32_t bagCount = 0, modCount = 0, genCount = 0;
};

inline void zones(ZoneLists& lists, const std::vector<Zone>& zones) {
  for (const auto& zone : zones) {
    le(lists.bags, lists.genCount, 2);
    le(lists.bags, lists.modCount, 2);
    ++lists.bagCount;
    for (const auto& g : zone.generators) {
      le(lists.gens, g.oper, 2);
      le(lists.gens, g.amount, 2);
      ++lists.genCount;
    }
    for (const auto& m : zone.modulators) {
      le(lists.mods, m.source, 2);
      le(lists.mods, m.destination, 2);
      le(lists.mods, static_cast<std::uint16_t>(m.amount), 2);
      le(lists.mods, m.amountSource, 2);
      le(lists.mods, m.transform, 2);
      ++lists.modCount;
    }
  }
}

inline void terminals(ZoneLists& lists) {
  le(lists.bags, lists.genCount, 2);
  le(lists.bags, lists.modCount, 2);
  lists.mods.insert(lists.mods.end(), 10, 0);
  lists.gens.insert(lists.gens.end(), 4, 0);
}

} // namespace detail

inline Parts parts(const File& file) {
  using namespace detail;
  Parts result;
  Bytes version;
  le(version, file.major, 2);
  le(version, file.minor, 2);
  result.info.push_back({"ifil", version});
  result.info.push_back({"INAM", Bytes{'t', 'e', 's', 't', 0, 0}});

  Bytes points, headers;
  for (const auto& sample : file.samples) {
    const auto start = static_cast<std::uint32_t>(points.size() / 2);
    for (const auto point : sample.points) le(points, static_cast<std::uint16_t>(point), 2);
    const auto end = static_cast<std::uint32_t>(points.size() / 2);
    points.insert(points.end(), 2 * 46, 0);
    name(headers, sample.name);
    le(headers, start, 4);
    le(headers, end, 4);
    const auto loop = sample.loop.value_or(Loop{0, end - start});
    le(headers, static_cast<std::uint32_t>(start + loop.start), 4);
    le(headers, static_cast<std::uint32_t>(start + loop.end), 4);
    le(headers, sample.rate, 4);
    headers.push_back(sample.originalPitch);
    headers.push_back(0); // pitch correction
    le(headers, 0, 2);    // sample link
    le(headers, sample.type, 2);
  }
  name(headers, "EOS");
  headers.insert(headers.end(), 26, 0);
  result.sdta.push_back({"smpl", points});
  if (file.sm24) result.sdta.push_back({"sm24", Bytes(points.size() / 2, 0)});

  ZoneLists presetLists, instrumentLists;
  Bytes presetHeaders, instrumentHeaders;
  for (const auto& preset : file.presets) {
    name(presetHeaders, preset.name);
    le(presetHeaders, preset.program, 2);
    le(presetHeaders, preset.bank, 2);
    le(presetHeaders, presetLists.bagCount, 2);
    presetHeaders.insert(presetHeaders.end(), 12, 0); // library, genre, morphology
    zones(presetLists, preset.zones);
  }
  name(presetHeaders, "EOP");
  presetHeaders.insert(presetHeaders.end(), 4, 0);
  le(presetHeaders, presetLists.bagCount, 2);
  presetHeaders.insert(presetHeaders.end(), 12, 0);
  terminals(presetLists);
  for (const auto& instrument : file.instruments) {
    name(instrumentHeaders, instrument.name);
    le(instrumentHeaders, instrumentLists.bagCount, 2);
    zones(instrumentLists, instrument.zones);
  }
  name(instrumentHeaders, "EOI");
  le(instrumentHeaders, instrumentLists.bagCount, 2);
  terminals(instrumentLists);

  result.pdta = {{"phdr", presetHeaders},        {"pbag", presetLists.bags},
                 {"pmod", presetLists.mods},     {"pgen", presetLists.gens},
                 {"inst", instrumentHeaders},    {"ibag", instrumentLists.bags},
                 {"imod", instrumentLists.mods}, {"igen", instrumentLists.gens},
                 {"shdr", headers}};
  return result;
}

inline Bytes assemble(const Parts& parts) {
  using namespace detail;
  Bytes form{'s', 'f', 'b', 'k'};
  chunk(form, "LIST", list("INFO", parts.info));
  chunk(form, "LIST", list("sdta", parts.sdta));
  chunk(form, "LIST", list("pdta", parts.pdta));
  Bytes result;
  chunk(result, "RIFF", form);
  return result;
}

inline Bytes build(const File& file) { return assemble(parts(file)); }

// Little-endian edits, for a test that corrupts one field of a written list.
inline void put16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
  bytes.at(offset) = static_cast<std::uint8_t>(value);
  bytes.at(offset + 1) = static_cast<std::uint8_t>(value >> 8);
}

inline void put32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
  for (int i = 0; i < 4; ++i)
    bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (8 * i));
}

} // namespace sf2test
