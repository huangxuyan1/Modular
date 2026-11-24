#include "modular/nodes/SoundFont.h"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace modular::nodes {
namespace {

[[noreturn]] void invalid(const std::string& reason) {
  throw std::invalid_argument("soundfont: " + reason);
}

// Little-endian and bounds-checked: every length is checked against its
// containing chunk, so a malformed chunk cannot read into the next one.
class Reader {
public:
  Reader(std::span<const std::uint8_t> bytes, std::size_t offset)
      : bytes_(bytes), offset_(offset) {}
  bool empty() const { return bytes_.empty(); }
  std::size_t size() const { return bytes_.size(); }
  std::size_t offset() const { return offset_; } // within the whole file
  Reader take(std::size_t count) {
    if (count > bytes_.size()) invalid("truncated chunk");
    Reader result(bytes_.first(count), offset_);
    bytes_ = bytes_.subspan(count);
    offset_ += count;
    return result;
  }
  std::string_view tag() {
    const auto bytes = take(4).bytes_;
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
  }
  std::uint32_t integer(std::size_t count) {
    std::uint32_t result = 0;
    const auto bytes = take(count).bytes_;
    for (std::size_t i = 0; i < count; ++i) result |= std::uint32_t(bytes[i]) << (8 * i);
    return result;
  }

private:
  std::span<const std::uint8_t> bytes_;
  std::size_t offset_;
};

struct Chunk {
  std::string_view tag;
  Reader data;
};

// A RIFF chunk: tag, size and data, padded to an even length.
Chunk chunk(Reader& parent) {
  const auto tag = parent.tag();
  const auto size = parent.integer(4);
  auto data = parent.take(size);
  if (size % 2 != 0) parent.take(1);
  return {tag, data};
}

// The hydra's nine lists in the specification's order, each a run of
// fixed-size records that ends with a terminal record.
enum HydraList : std::size_t {
  kPhdr, kPbag, kPmod, kPgen, kInst, kIbag, kImod, kIgen, kShdr
};
constexpr std::size_t kHydraLists = 9;
constexpr std::string_view kListTags[kHydraLists] = {"phdr", "pbag", "pmod", "pgen", "inst",
                                                     "ibag", "imod", "igen", "shdr"};
constexpr std::size_t kRecordSizes[kHydraLists] = {38, 4, 10, 4, 22, 4, 10, 4, 46};

using Generators = std::vector<std::pair<std::uint16_t, std::uint16_t>>;

// Where each record's run starts in the next list: a preset's bags, a bag's
// generators. The starts never fall, and the terminal record's start is the
// next list's terminal record, so every run lies inside that list and every
// record of it belongs to a run.
void checkRuns(const std::vector<std::uint16_t>& starts, std::size_t count,
               std::string_view list) {
  for (std::size_t i = 1; i < starts.size(); ++i)
    if (starts[i] < starts[i - 1]) invalid(std::string(list) + " indexes out of order");
  if (starts.back() + std::size_t{1} != count)
    invalid(std::string(list) + " indexes do not end at the list's end");
}

// The specification's defaults (section 8.1.3), where they are not zero.
constexpr auto kGeneratorDefaults = [] {
  std::array<std::int16_t, SoundFont::kGeneratorCount> defaults{};
  defaults[8] = 13500; // initialFilterFc
  for (const int stage : {21, 23, 25, 26, 27, 28, 30, 33, 34, 35, 36, 38})
    defaults[stage] = -12000; // LFO delays and envelope times
  defaults[SoundFont::kKeyRange] = defaults[SoundFont::kVelRange] = 0x7f00; // 0 to 127
  defaults[46] = defaults[47] = defaults[SoundFont::kOverridingRootKey] = -1; // unset
  defaults[56] = 100; // scaleTuning
  return defaults;
}();

// The specification's ranges (section 8.1.3), for the generators that have one.
struct Bounds {
  bool bounded = false;
  std::int32_t low = 0, high = 0;
};
constexpr auto kGeneratorBounds = [] {
  std::array<Bounds, SoundFont::kGeneratorCount> bounds{};
  const auto bound = [&](std::initializer_list<int> generators, int low, int high) {
    for (const int g : generators) bounds[g] = {true, low, high};
  };
  bound({5, 6, 7, 10, 11}, -12000, 12000);       // pitch and cutoff depths
  bound({8}, 1500, 13500);                       // initialFilterFc
  bound({9}, 0, 960);                            // initialFilterQ
  bound({13}, -960, 960);                        // modLfoToVolume
  bound({15, 16}, 0, 1000);                      // effects sends
  bound({17}, -500, 500);                        // pan
  bound({21, 23, 25, 27, 33, 35}, -12000, 5000); // delays and holds
  bound({22, 24}, -16000, 4500);                 // LFO frequencies
  bound({26, 28, 30, 34, 36, 38}, -12000, 8000); // attacks, decays and releases
  bound({29}, 0, 1000);                          // sustainModEnv
  bound({31, 32, 39, 40}, -1200, 1200);          // key scaling
  bound({37, 48}, 0, 1440);                      // sustainVolEnv, initialAttenuation
  bound({46, 47, 58}, -1, 127);                  // keynum, velocity, root; -1 is unset
  bound({51}, -120, 120);                        // coarseTune
  bound({52}, -99, 99);                          // fineTune
  bound({56}, 0, 1200);                          // scaleTuning
  bound({57}, 0, 127);                           // exclusiveClass
  return bounds;
}();

// Generators valid only in an instrument; a preset's are ignored.
constexpr auto kInstrumentOnly = [] {
  std::array<bool, SoundFont::kGeneratorCount> only{};
  for (const int g : {0, 1, 2, 3, 4, 12, 45, 46, 47, 50, 53, 54, 57, 58}) only[g] = true;
  return only;
}();

// What a load lists as ignored or skipped, each kind once, in this order, and
// the entry it lists for each.
enum Listing : std::size_t {
  kSends, kLinks, kRomZones, kEmptyZones, kRatelessZones, kLoopsOutside, kEmptyLoops,
  kListingCount
};
constexpr std::string_view kListingEntries[kListingCount] = {
    "soundfont: reverb and chorus sends are ignored",
    "soundfont: link modulators are ignored",
    "soundfont: zones playing a sample in rom are skipped",
    "soundfont: zones playing an empty sample are skipped",
    "soundfont: zones playing a sample with no sample rate are skipped",
    "soundfont: loops outside their sample play unlooped",
    "soundfont: empty loops play unlooped",
};

// A modulator is part of a link (2.04) when its source is another modulator's
// output, the link controller 127, or its output is another modulator's
// source, a destination with bit 15 set. A link as an amount source is
// illegal, and like every illegal modulator it is ignored unlisted.
bool isLink(std::uint16_t source, std::uint16_t destination) {
  return (source & 0xff) == 0x7f || (destination & 0x8000) != 0;
}

// A zone's own value for a generator, else its global zone's, else none. The
// zones are SoundFont's private ParsedZone.
std::optional<std::int16_t> given(const auto& local, const auto& global, std::size_t g) {
  if (local.given[g]) return local.amounts[g];
  if (global && global->given[g]) return global->amounts[g];
  return std::nullopt;
}

} // namespace

SoundFont SoundFont::read(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) invalid("cannot open input file");
  const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
  if (file.bad()) invalid("failed to read input file");
  return parse(bytes);
}

SoundFont SoundFont::parse(std::span<const std::uint8_t> bytes) {
  SoundFont result;
  result.bytes_.assign(bytes.begin(), bytes.end());
  Reader file(result.bytes_, 0);
  auto riff = chunk(file);
  // Bytes after the RIFF chunk are not part of the file, and are ignored.
  if (riff.tag != "RIFF") invalid("missing RIFF header");
  if (riff.data.tag() != "sfbk") invalid("not a soundfont");

  // The form's three lists, by type. Other chunks are skipped by their sizes.
  std::optional<Reader> info, sdta, pdta;
  while (!riff.data.empty()) {
    auto list = chunk(riff.data);
    if (list.tag != "LIST") continue;
    const auto type = list.data.tag();
    auto* slot = type == "INFO"   ? &info
                 : type == "sdta" ? &sdta
                 : type == "pdta" ? &pdta
                                  : nullptr;
    if (slot == nullptr) continue;
    if (*slot) invalid("duplicate " + std::string(type) + " list");
    *slot = list.data;
  }
  if (!info) invalid("missing INFO list");
  if (!sdta) invalid("missing sdta list");
  if (!pdta) invalid("missing pdta list");

  std::optional<Reader> version;
  while (!info->empty()) {
    auto sub = chunk(*info);
    if (sub.tag == "ifil") version = sub.data;
  }
  if (!version) invalid("missing ifil version");
  if (version->size() != 4) invalid("ifil version of the wrong size");
  const auto major = version->integer(2);
  if (major == 3) invalid("compressed samples (sf3) are not supported");
  if (major != 2) invalid("only version 2 files are supported");

  // 16-bit sample data. A 24-bit extension (sm24) is skipped, so a 24-bit file
  // plays at 16 bits, as the specification allows. No smpl chunk is no data.
  std::size_t points = 0;
  while (!sdta->empty()) {
    auto sub = chunk(*sdta);
    if (sub.tag != "smpl") continue;
    if (sub.data.size() % 2 != 0) invalid("sample data of odd size");
    result.sample_data_ = sub.data.offset();
    points = sub.data.size() / 2;
  }

  std::optional<Reader> lists[kHydraLists];
  while (!pdta->empty()) {
    auto sub = chunk(*pdta);
    for (std::size_t i = 0; i < kHydraLists; ++i) {
      if (sub.tag != kListTags[i]) continue;
      if (lists[i]) invalid("duplicate " + std::string(kListTags[i]) + " list");
      lists[i] = sub.data;
    }
  }
  std::size_t counts[kHydraLists];
  for (std::size_t i = 0; i < kHydraLists; ++i) {
    if (!lists[i]) invalid("missing " + std::string(kListTags[i]) + " list");
    if (lists[i]->size() % kRecordSizes[i] != 0 || lists[i]->size() < kRecordSizes[i])
      invalid(std::string(kListTags[i]) + " list of the wrong size");
    counts[i] = lists[i]->size() / kRecordSizes[i];
  }

  // Samples first: the instruments' zones are checked against them.
  {
    Reader list = *lists[kShdr];
    for (std::size_t i = 0; i + 1 < counts[kShdr]; ++i) {
      list.take(20); // name
      Sample sample;
      sample.start = list.integer(4);
      sample.end = list.integer(4);
      sample.loopStart = list.integer(4);
      sample.loopEnd = list.integer(4);
      sample.rate = list.integer(4);
      sample.originalPitch = static_cast<std::uint8_t>(list.integer(1));
      sample.correction = static_cast<std::int8_t>(list.integer(1)); // signed cents
      list.take(2);                                                  // link
      const auto type = list.integer(2);
      if ((type & 0x10) != 0) invalid("compressed samples (sf3) are not supported");
      sample.rom = (type & 0x8000) != 0;
      result.samples_.push_back(sample);
    }
    for (const auto& sample : result.samples_)
      if (!sample.rom && (sample.start > sample.end || sample.end > points))
        invalid("a sample lies outside the sample data");
  }

  // Which kinds of thing the file uses that the load lists.
  std::array<bool, kListingCount> listed{};

  // A bag holds where its zone's generators and modulators start; zone z runs
  // from bag z to bag z + 1. Modulators' runs are checked like the
  // generators', but a modulator is read only to find links.
  struct Bags {
    std::vector<std::uint16_t> generators, modulators;
  };
  const auto readBags = [&](HydraList bagList, HydraList modulatorList,
                            HydraList generatorList, std::string_view level) {
    Reader list = *lists[bagList];
    Bags bags;
    for (std::size_t i = 0; i < counts[bagList]; ++i) {
      bags.generators.push_back(static_cast<std::uint16_t>(list.integer(2)));
      bags.modulators.push_back(static_cast<std::uint16_t>(list.integer(2)));
    }
    checkRuns(bags.generators, counts[generatorList], std::string(level) + " generator");
    checkRuns(bags.modulators, counts[modulatorList], std::string(level) + " modulator");
    return bags;
  };
  const auto readGenerators = [&](HydraList generatorList) {
    Reader list = *lists[generatorList];
    Generators generators(counts[generatorList]);
    for (auto& [oper, amount] : generators) {
      oper = static_cast<std::uint16_t>(list.integer(2));
      amount = static_cast<std::uint16_t>(list.integer(2));
    }
    return generators;
  };
  // Whether each modulator is part of a link.
  const auto readLinks = [&](HydraList modulatorList) {
    Reader list = *lists[modulatorList];
    std::vector<bool> links(counts[modulatorList]);
    for (std::size_t i = 0; i < links.size(); ++i) {
      auto record = list.take(kRecordSizes[modulatorList]);
      const auto source = static_cast<std::uint16_t>(record.integer(2));
      links[i] = isLink(source, static_cast<std::uint16_t>(record.integer(2)));
    }
    return links;
  };
  // A preset's or instrument's header: where its bags start. `each` reads
  // what comes before that index in the record.
  const auto readHeaders = [&](HydraList headerList, HydraList bagList,
                               std::string_view level, auto&& each) {
    Reader list = *lists[headerList];
    std::vector<std::uint16_t> bags(counts[headerList]);
    for (auto& bag : bags) {
      auto record = list.take(kRecordSizes[headerList]);
      each(record);
      bag = static_cast<std::uint16_t>(record.integer(2));
    }
    checkRuns(bags, counts[bagList], std::string(level) + " bag");
    return bags;
  };

  // Zones [first, last) of one preset or instrument. A first zone without a
  // terminal generator is the global zone; a later one is ignored. Generators
  // after the terminal one are ignored; a repeated one keeps its last value.
  // A zone read lists its effects sends above 0 and its link modulators.
  const auto readZones = [&listed](const Bags& bags, const Generators& generators,
                                   const std::vector<bool>& links, std::size_t first,
                                   std::size_t last, std::uint16_t terminal,
                                   std::size_t targets, std::string_view target) {
    Zones zones;
    for (std::size_t bag = first; bag < last; ++bag) {
      ParsedZone zone;
      bool terminated = false;
      for (std::size_t g = bags.generators[bag];
           g < bags.generators[bag + 1] && !terminated; ++g) {
        const auto [oper, amount] = generators[g];
        if (oper == terminal) {
          if (amount >= targets) invalid(std::string(target) + " index out of range");
          zone.target = amount;
          terminated = true;
        } else if (oper < kGeneratorCount) {
          zone.amounts[oper] = static_cast<std::int16_t>(amount);
          zone.given[oper] = true;
        }
      }
      if (!terminated && bag != first) continue;
      for (const auto send : {kChorusEffectsSend, kReverbEffectsSend})
        if (zone.given[send] && zone.amounts[send] > 0) listed[kSends] = true;
      for (std::size_t m = bags.modulators[bag]; m < bags.modulators[bag + 1]; ++m)
        if (links[m]) listed[kLinks] = true;
      if (terminated) zones.local.push_back(zone);
      else zones.global = zone;
    }
    return zones;
  };

  {
    const auto bags = readBags(kIbag, kImod, kIgen, "instrument");
    const auto generators = readGenerators(kIgen);
    const auto links = readLinks(kImod);
    const auto starts = readHeaders(kInst, kIbag, "instrument",
                                    [](Reader& record) { record.take(20); }); // name
    for (std::size_t i = 0; i + 1 < starts.size(); ++i) {
      auto instrument = readZones(bags, generators, links, starts[i], starts[i + 1],
                                  kSampleId, result.samples_.size(), "sample");
      // A zone whose sample cannot play is skipped, so the rest of the file
      // plays; the others get the points they play.
      std::vector<ParsedZone> playable;
      for (auto& zone : instrument.local) {
        const auto& sample = result.samples_[zone.target];
        const auto unplayable = [&]() -> std::optional<Listing> {
          if (sample.rom) return kRomZones;
          if (sample.start == sample.end) return kEmptyZones;
          if (sample.rate == 0) return kRatelessZones;
          return std::nullopt;
        }();
        if (unplayable) {
          listed[*unplayable] = true;
          continue;
        }
        zone.points = place(zone, instrument.global, sample);
        if (zone.points.fault == Points::Fault::kLoopOutside) listed[kLoopsOutside] = true;
        if (zone.points.fault == Points::Fault::kLoopEmpty) listed[kEmptyLoops] = true;
        playable.push_back(zone);
      }
      instrument.local = std::move(playable);
      result.instruments_.push_back(std::move(instrument));
    }
  }
  {
    const auto bags = readBags(kPbag, kPmod, kPgen, "preset");
    const auto generators = readGenerators(kPgen);
    const auto links = readLinks(kPmod);
    std::vector<PresetNumber> numbers;
    const auto starts = readHeaders(kPhdr, kPbag, "preset", [&](Reader& record) {
      record.take(20); // name
      PresetNumber number;
      number.program = static_cast<std::uint16_t>(record.integer(2));
      number.bank = static_cast<std::uint16_t>(record.integer(2));
      numbers.push_back(number);
    });
    if (starts.size() < 2) invalid("no preset");
    for (std::size_t p = 0; p + 1 < starts.size(); ++p) {
      result.presets_.push_back({numbers[p], readZones(bags, generators, links, starts[p],
                                                       starts[p + 1], kInstrument,
                                                       result.instruments_.size(),
                                                       "instrument")});
    }
  }
  for (std::size_t listing = 0; listing < kListingCount; ++listing)
    if (listed[listing]) result.ignored_.emplace_back(kListingEntries[listing]);
  return result;
}

// The points a zone plays: the sample header's start, end and loop, each plus
// the zone's address offsets, the fine offset plus 32768 times the coarse one.
// The start and end stay within the header's sample. A loop that lies outside
// the points played, or is empty (its end not after its start), is dropped, so
// the zone plays unlooped; a zone that would loop (sampleModes 1 or 3) says why.
SoundFont::Points SoundFont::place(const ParsedZone& zone,
                                   const std::optional<ParsedZone>& global,
                                   const Sample& sample) {
  // Every generator here is valid only in an instrument, so the instrument's
  // value is the zone's.
  const auto value = [&](Generator g) -> std::int64_t {
    return given(zone, global, g).value_or(kGeneratorDefaults[g]);
  };
  const auto address = [&](std::uint32_t header, Generator fine, Generator coarse) {
    return std::int64_t{header} + value(fine) + std::int64_t{32768} * value(coarse);
  };
  const auto withinSample = [&](std::int64_t point) {
    return static_cast<std::uint32_t>(
        std::clamp<std::int64_t>(point, sample.start, sample.end));
  };
  Points points;
  points.start =
      withinSample(address(sample.start, kStartAddrsOffset, kStartAddrsCoarseOffset));
  points.end = std::max(points.start, withinSample(address(sample.end, kEndAddrsOffset,
                                                           kEndAddrsCoarseOffset)));
  const auto loopStart =
      address(sample.loopStart, kStartloopAddrsOffset, kStartloopAddrsCoarseOffset);
  const auto loopEnd =
      address(sample.loopEnd, kEndloopAddrsOffset, kEndloopAddrsCoarseOffset);
  const auto played = [&](std::int64_t point) {
    return points.start <= point && point <= points.end;
  };
  const bool inside = played(loopStart) && played(loopEnd);
  if (inside && loopStart < loopEnd) {
    points.loopStart = static_cast<std::uint32_t>(loopStart);
    points.loopEnd = static_cast<std::uint32_t>(loopEnd);
  } else if (const auto mode = value(kSampleModes); mode == 1 || mode == 3) {
    points.fault = inside ? Points::Fault::kLoopEmpty : Points::Fault::kLoopOutside;
  }
  return points;
}

SoundFont::PresetNumber SoundFont::lowestPreset() const {
  PresetNumber lowest = presets_.front().number;
  for (const auto& p : presets_)
    if (p.number.bank < lowest.bank ||
        (p.number.bank == lowest.bank && p.number.program < lowest.program))
      lowest = p.number;
  return lowest;
}

std::vector<SoundFont::Zone> SoundFont::zones(PresetNumber number) const {
  const auto preset = std::find_if(presets_.begin(), presets_.end(),
                                   [&](const Preset& p) { return p.number == number; });
  if (preset == presets_.end())
    invalid("no preset at bank " + std::to_string(number.bank) + " program " +
            std::to_string(number.program));

  // A range's low and high bytes, intersected with another range.
  const auto intersect = [](std::int16_t range, std::uint8_t& low, std::uint8_t& high) {
    low = std::max(low, static_cast<std::uint8_t>(range & 0xff));
    high = std::min(high, static_cast<std::uint8_t>((range >> 8) & 0xff));
  };

  std::vector<Zone> result;
  for (const auto& presetZone : preset->zones.local) {
    const auto& instrument = instruments_[presetZone.target];
    for (const auto& instrumentZone : instrument.local) {
      Zone zone;
      for (std::size_t g = 0; g < kGeneratorCount; ++g) {
        const auto value = given(instrumentZone, instrument.global, g);
        const auto offset = given(presetZone, preset->zones.global, g);
        if (g == kKeyRange || g == kVelRange) {
          auto& low = g == kKeyRange ? zone.keyLow : zone.velocityLow;
          auto& high = g == kKeyRange ? zone.keyHigh : zone.velocityHigh;
          if (value) intersect(*value, low, high);
          if (offset) intersect(*offset, low, high);
          continue;
        }
        std::int32_t total = value ? *value : kGeneratorDefaults[g];
        if (offset && !kInstrumentOnly[g]) total += *offset;
        const auto& bounds = kGeneratorBounds[g];
        if (bounds.bounded) total = std::clamp(total, bounds.low, bounds.high);
        zone.generators[g] = total;
      }
      if (zone.keyLow > zone.keyHigh || zone.velocityLow > zone.velocityHigh) continue;
      const auto& sample = samples_[instrumentZone.target];
      const auto& points = instrumentZone.points;
      zone.sampleStart = points.start;
      zone.sampleEnd = points.end;
      zone.loopStart = points.loopStart;
      zone.loopEnd = points.loopEnd;
      zone.sampleRate = sample.rate;
      zone.originalPitch = sample.originalPitch;
      zone.pitchCorrection = sample.correction;
      result.push_back(zone);
    }
  }
  return result;
}

} // namespace modular::nodes
