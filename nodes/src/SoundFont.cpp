#include "modular/nodes/SoundFont.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

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

// The hydra's nine lists, located by tag, each a run of fixed-size records.
struct Hydra {
  static constexpr std::string_view kTags[] = {"phdr", "pbag", "pmod", "pgen", "inst",
                                               "ibag", "imod", "igen", "shdr"};
  static constexpr std::size_t kRecordSizes[] = {38, 4, 10, 4, 22, 4, 10, 4, 46};
  std::optional<Reader> lists[9];

  Reader records(std::size_t list, std::size_t& count) const {
    const auto& data = *lists[list];
    if (data.size() % kRecordSizes[list] != 0 || data.size() < kRecordSizes[list])
      invalid(std::string(kTags[list]) + " list of the wrong size");
    count = data.size() / kRecordSizes[list];
    return data;
  }
};

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
  if (riff.tag != "RIFF") invalid("missing RIFF header");
  if (riff.data.tag() != "sfbk") invalid("not a soundfont");

  std::optional<Reader> info, sdta, pdta;
  while (!riff.data.empty()) {
    auto list = chunk(riff.data);
    if (list.tag != "LIST") continue;
    const auto type = list.data.tag();
    auto& slot = type == "INFO" ? info : type == "sdta" ? sdta : pdta;
    if (type != "INFO" && type != "sdta" && type != "pdta") continue;
    slot = list.data;
  }
  if (!info || !sdta || !pdta) invalid("missing INFO, sdta or pdta list");

  std::size_t samplePoints = 0;
  while (!sdta->empty()) {
    auto sub = chunk(*sdta);
    if (sub.tag != "smpl") continue;
    result.sample_data_ = sub.data.offset();
    samplePoints = sub.data.size() / 2;
  }

  Hydra hydra;
  while (!pdta->empty()) {
    auto sub = chunk(*pdta);
    for (std::size_t i = 0; i < 9; ++i)
      if (sub.tag == Hydra::kTags[i]) hydra.lists[i] = sub.data;
  }

  std::size_t counts[9];
  Reader lists[9] = {hydra.records(0, counts[0]), hydra.records(1, counts[1]),
                     hydra.records(2, counts[2]), hydra.records(3, counts[3]),
                     hydra.records(4, counts[4]), hydra.records(5, counts[5]),
                     hydra.records(6, counts[6]), hydra.records(7, counts[7]),
                     hydra.records(8, counts[8])};

  // Bags hold [generator, modulator] indexes; zone z spans bag z to bag z + 1.
  const auto readBags = [&](Reader list, std::size_t count) {
    std::vector<std::uint16_t> generators(count);
    for (std::size_t i = 0; i < count; ++i) {
      generators[i] = static_cast<std::uint16_t>(list.integer(2));
      list.integer(2);
    }
    return generators;
  };
  const auto readGenerators = [&](Reader list, std::size_t count) {
    std::vector<std::pair<std::uint16_t, std::uint16_t>> generators(count);
    for (auto& g : generators) {
      g.first = static_cast<std::uint16_t>(list.integer(2));
      g.second = static_cast<std::uint16_t>(list.integer(2));
    }
    return generators;
  };
  const auto presetBags = readBags(lists[1], counts[1]);
  const auto presetGenerators = readGenerators(lists[3], counts[3]);
  const auto instrumentBags = readBags(lists[5], counts[5]);
  const auto instrumentGenerators = readGenerators(lists[7], counts[7]);

  // Zones [first, last) of one preset or instrument, the first possibly global.
  const auto readZones = [&](const std::vector<std::uint16_t>& bags,
                             const std::vector<std::pair<std::uint16_t, std::uint16_t>>& gens,
                             std::size_t first, std::size_t last, std::uint16_t terminal,
                             bool& hasGlobal, ParsedZone& global,
                             std::vector<ParsedZone>& zones) {
    for (std::size_t bag = first; bag < last; ++bag) {
      ParsedZone zone;
      bool terminated = false;
      for (std::size_t g = bags[bag]; g < bags[bag + 1] && !terminated; ++g) {
        const auto [oper, amount] = gens[g];
        if (oper == terminal) {
          zone.target = amount;
          terminated = true;
        } else if (oper < kGeneratorCount) {
          zone.amounts[oper] = static_cast<std::int16_t>(amount);
          zone.given[oper] = true;
        }
      }
      if (terminated) zones.push_back(zone);
      else if (bag == first) {
        hasGlobal = true;
        global = zone;
      }
    }
  };

  {
    Reader list = lists[0];
    std::vector<std::pair<PresetNumber, std::size_t>> headers(counts[0]);
    for (auto& [number, bag] : headers) {
      list.take(20);
      number.program = static_cast<std::uint16_t>(list.integer(2));
      number.bank = static_cast<std::uint16_t>(list.integer(2));
      bag = list.integer(2);
      list.take(12);
    }
    for (std::size_t p = 0; p + 1 < headers.size(); ++p) {
      Preset preset;
      preset.number = headers[p].first;
      readZones(presetBags, presetGenerators, headers[p].second, headers[p + 1].second,
                kInstrument, preset.hasGlobal, preset.global, preset.zones);
      result.presets_.push_back(std::move(preset));
    }
  }
  {
    Reader list = lists[4];
    std::vector<std::size_t> bags(counts[4]);
    for (auto& bag : bags) {
      list.take(20);
      bag = list.integer(2);
    }
    for (std::size_t i = 0; i + 1 < bags.size(); ++i) {
      Instrument instrument;
      readZones(instrumentBags, instrumentGenerators, bags[i], bags[i + 1], kSampleId,
                instrument.hasGlobal, instrument.global, instrument.zones);
      result.instruments_.push_back(std::move(instrument));
    }
  }
  {
    Reader list = lists[8];
    for (std::size_t i = 0; i + 1 < counts[8]; ++i) {
      list.take(20);
      Sample sample;
      sample.start = list.integer(4);
      sample.end = list.integer(4);
      list.integer(4);
      list.integer(4);
      sample.rate = list.integer(4);
      sample.originalPitch = static_cast<std::uint8_t>(list.integer(1));
      list.take(5);
      result.samples_.push_back(sample);
    }
  }
  (void)samplePoints;
  return result;
}

bool SoundFont::hasPreset(PresetNumber preset) const {
  return std::any_of(presets_.begin(), presets_.end(), [&](const Preset& p) {
    return p.number.bank == preset.bank && p.number.program == preset.program;
  });
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
  const auto preset = std::find_if(presets_.begin(), presets_.end(), [&](const Preset& p) {
    return p.number.bank == number.bank && p.number.program == number.program;
  });
  if (preset == presets_.end())
    invalid("no preset at bank " + std::to_string(number.bank) + " program " +
            std::to_string(number.program));

  std::vector<Zone> result;
  for (const auto& presetZone : preset->zones) {
    const auto& instrument = instruments_[presetZone.target];
    for (const auto& instrumentZone : instrument.zones) {
      std::int32_t values[kGeneratorCount];
      for (std::size_t g = 0; g < kGeneratorCount; ++g) {
        auto value = kGeneratorDefaults[g];
        if (instrument.hasGlobal && instrument.global.given[g])
          value = instrument.global.amounts[g];
        if (instrumentZone.given[g]) value = instrumentZone.amounts[g];
        values[g] = value;
      }
      Zone zone;
      for (std::size_t g = 0; g < kGeneratorCount; ++g) zone.generators[g] = values[g];
      const auto range = [](std::int32_t value, std::uint8_t& low, std::uint8_t& high) {
        low = static_cast<std::uint8_t>(value & 0xff);
        high = static_cast<std::uint8_t>((value >> 8) & 0xff);
      };
      range(values[kKeyRange], zone.keyLow, zone.keyHigh);
      range(values[kVelRange], zone.velocityLow, zone.velocityHigh);
      const auto& sample = samples_[instrumentZone.target];
      zone.sampleStart = sample.start;
      zone.sampleEnd = sample.end;
      zone.sampleRate = sample.rate;
      zone.originalPitch = sample.originalPitch;
      result.push_back(zone);
    }
  }
  return result;
}

} // namespace modular::nodes
