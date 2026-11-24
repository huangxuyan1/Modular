#include "modular/nodes/PrimitivePersistence.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <typeinfo>

#include "modular/nodes/BasicNodes.h"
#include "modular/nodes/MidiFileNode.h"
#include "modular/nodes/ManualTriggerNode.h"
#include "modular/nodes/SoundFontNode.h"
#include "modular/nodes/WavRecorderNode.h"
#ifdef _WIN32
#include "modular/nodes/AudioOutputNode.h"
#endif

namespace modular::nodes {
namespace {

// Node-owned text inside the container's opaque bytes. Decimal timestamps use
// max_digits10 (17) for the authored double, never prepared sample frames.
class ContentReader {
public:
  explicit ContentReader(const NodeContent& content) : size_(content.stored.size()) {
    input_.imbue(std::locale::classic());
    std::string text;
    text.reserve(size_);
    for (const auto byte : content.stored) text += std::to_integer<char>(byte);
    input_.str(std::move(text));
  }
  template <class T> T number() {
    std::string text;
    T value{};
    if (!(input_ >> text))
      throw std::invalid_argument("open: missing stored node content");
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
      throw std::invalid_argument("open: invalid stored node number");
    return value;
  }
  std::size_t count() {
    const auto count = number<std::size_t>();
    if (count > size_) throw std::invalid_argument("open: stored count exceeds content");
    return count;
  }
  MidiMsg message() {
    const auto status = number<std::uint8_t>();
    const auto data1 = number<std::uint8_t>();
    const auto data2 = number<std::uint8_t>();
    return {status, data1, data2};
  }
  void end() {
    input_ >> std::ws;
    if (!input_.eof()) throw std::invalid_argument("open: trailing stored node content");
  }
private:
  std::size_t size_;
  std::istringstream input_;
};

NodeContent storedText(std::string settings, const std::string& text) {
  NodeContent content{std::move(settings), {}};
  content.stored.reserve(text.size());
  for (const auto c : text) content.stored.push_back(static_cast<std::byte>(c));
  return content;
}

void writeMessage(std::ostream& text, MidiMsg message) {
  text << ' ' << unsigned(message.status) << ' ' << unsigned(message.data1)
       << ' ' << unsigned(message.data2) << '\n';
}

void requireEmpty(const NodeContent& content) {
  if (!content.settings.empty() || !content.stored.empty())
    throw std::invalid_argument("open: unexpected node content");
}

void validateBlock(std::span<const MidiBlockNode::Entry> entries) {
  std::uint64_t previous = 0;
  for (const auto& entry : entries) {
    if (entry.offsetFrames < previous || !entry.msg.isValidChannelMessage())
      throw std::invalid_argument("midi block: invalid stored order or channel message");
    previous = entry.offsetFrames;
  }
}

template <class T>
T setting(const NodeContent& content) {
  T value{};
  const auto& text = content.settings;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (!content.stored.empty() || result.ec != std::errc{} ||
      result.ptr != text.data() + text.size())
    throw std::invalid_argument("open: invalid primitive authored content");
  return value;
}

void requirePorts(const Node& node, std::span<const PortType> inputs,
                  std::span<const PortType> outputs) {
  const auto check = [](std::span<const PortSpec> ports, std::span<const PortType> types) {
    if (ports.size() != types.size())
      throw std::invalid_argument("save: unsupported primitive port configuration");
    for (std::size_t i = 0; i < ports.size(); ++i)
      if (ports[i].type != types[i])
        throw std::invalid_argument("save: unsupported primitive port type");
  };
  check(node.inputs(), inputs);
  check(node.outputs(), outputs);
}

// modular.soundfont/1 SETTINGS: "loaded bank program voiceLimit", or "empty
// voiceLimit" for a node with no soundfont, in classic-locale decimal.
struct SoundFontSettings {
  std::optional<SoundFont::PresetNumber> preset; // none for an empty node
  std::uint32_t voiceLimit = 0;
};

SoundFontSettings soundFontSettings(const std::string& text) {
  const auto invalid = [] {
    return std::invalid_argument("open: invalid soundfont settings");
  };
  std::istringstream input(text);
  input.imbue(std::locale::classic());
  const auto read = [&](auto& value) {
    std::string token;
    if (!(input >> token)) throw invalid();
    const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
    if (result.ec != std::errc{} || result.ptr != token.data() + token.size())
      throw invalid();
  };
  SoundFontSettings settings;
  std::string state;
  input >> state;
  if (state == "loaded") {
    settings.preset.emplace();
    read(settings.preset->bank);
    read(settings.preset->program);
  } else if (state != "empty") {
    throw invalid();
  }
  read(settings.voiceLimit);
  input >> std::ws;
  if (!input.eof()) throw invalid();
  return settings;
}

} // namespace

std::span<const NodeCodec> primitiveCodecs() {
  static const NodeCodec codecs[]{
    {"modular.wav-recorder/1",
     [](const Node& n) { return typeid(n) == typeid(WavRecorderNode); },
     [](const Node& n) {
       const auto& node = static_cast<const WavRecorderNode&>(n);
       const std::array inputs{PortType::audio(node.channels()),
           PortType::event(subtype::Trigger), PortType::event(subtype::Trigger)};
       requirePorts(node, inputs, {});
       const auto path = node.directory().u8string();
       std::ostringstream settings;
       settings.imbue(std::locale::classic());
       settings << node.channels() << ' ' << std::quoted(std::string(path.begin(), path.end()))
                << ' ' << std::quoted(node.prefix());
       return NodeContent{settings.str(), {}};
     },
     [](Graph& g, NodeId id, const NodePorts& ports, const NodeContent& content) {
       std::istringstream settings(content.settings);
       settings.imbue(std::locale::classic());
       std::uint32_t channels = 0;
       std::string directory, prefix;
       if (!content.stored.empty() ||
           !(settings >> channels >> std::quoted(directory) >> std::quoted(prefix)))
         throw std::invalid_argument("open: invalid recorder settings");
       settings >> std::ws;
       if (!settings.eof()) throw std::invalid_argument("open: trailing recorder settings");
       return g.restoreNode<WavRecorderNode>(id, ports,
           std::filesystem::u8path(directory), std::move(prefix), channels);
     }},
    {"modular.const-float/1",
     [](const Node& n) { return typeid(n) == typeid(ConstFloatNode); },
     [](const Node& n) {
       const auto& node = static_cast<const ConstFloatNode&>(n);
       const std::array outputs{PortType::value(subtype::Float)};
       requirePorts(node, {}, outputs);
       std::ostringstream text;
       text.imbue(std::locale::classic());
       text << std::setprecision(std::numeric_limits<float>::max_digits10)
            << node.constant();
       return NodeContent{text.str(), {}};
     },
     [](Graph& g, NodeId id, const NodePorts& ports, const NodeContent& content) {
       return g.restoreNode<ConstFloatNode>(id, ports, setting<float>(content));
     }},
    {"modular.gain/1",
     [](const Node& n) { return typeid(n) == typeid(GainNode); },
     [](const Node& n) {
       const auto& node = static_cast<const GainNode&>(n);
       const std::array inputs{PortType::audio(node.channels()),
                               PortType::value(subtype::Float)};
       const std::array outputs{PortType::audio(node.channels())};
       requirePorts(node, inputs, outputs);
       return NodeContent{std::to_string(node.channels()), {}};
     },
     [](Graph& g, NodeId id, const NodePorts& ports, const NodeContent& content) {
       return g.restoreNode<GainNode>(id, ports, setting<std::uint32_t>(content));
     }},
    {"modular.mix/1",
     [](const Node& n) { return typeid(n) == typeid(MixNode); },
     [](const Node& n) {
       const auto& node = static_cast<const MixNode&>(n);
       if (node.inputCount() == 0)
         throw std::invalid_argument("save: mixer needs at least one input");
       const std::vector inputs(node.inputCount(), PortType::audio(node.channels()));
       const std::array outputs{PortType::audio(node.channels())};
       requirePorts(node, inputs, outputs);
       return NodeContent{std::to_string(node.channels()), {}};
     },
     [](Graph& g, NodeId id, const NodePorts& ports, const NodeContent& content) {
       return g.restoreNode<MixNode>(id, ports,
           static_cast<std::uint32_t>(ports.inputs.size()), setting<std::uint32_t>(content));
     }},
    {"modular.manual-trigger/1",
     [](const Node& n) { return typeid(n) == typeid(ManualTriggerNode); },
     [](const Node& n) {
       const auto& node = static_cast<const ManualTriggerNode&>(n);
       const std::array outputs{PortType::event(subtype::Trigger)};
       requirePorts(node, {}, outputs);
       return NodeContent{node.label() == ManualTriggerNode::Label::Start ? "start" : "stop", {}};
     },
     [](Graph& g, NodeId id, const NodePorts& ports, const NodeContent& content) {
       if (!content.stored.empty() ||
           (content.settings != "start" && content.settings != "stop"))
         throw std::invalid_argument("open: invalid manual trigger label");
       return g.restoreNode<ManualTriggerNode>(id, ports, content.settings == "start"
           ? ManualTriggerNode::Label::Start : ManualTriggerNode::Label::Stop);
     }},
    {"modular.sine-synth/1",
     [](const Node& n) { return typeid(n) == typeid(SineSynthNode); },
     [](const Node& node) {
       const std::array inputs{PortType::event(subtype::Midi)};
       const std::array outputs{PortType::audio(1)};
       requirePorts(node, inputs, outputs);
       return NodeContent{};
     },
     [](Graph& g, NodeId id, const NodePorts& ports, const NodeContent& content) {
       requireEmpty(content);
       return g.restoreNode<SineSynthNode>(id, ports);
     }},
    {"modular.voice-allocator/1",
     [](const Node& n) { return typeid(n) == typeid(VoiceAllocatorNode); },
     [](const Node& n) {
       const auto& node = static_cast<const VoiceAllocatorNode&>(n);
       if (node.voiceCount() == 0)
         throw std::invalid_argument("save: allocator needs at least one voice");
       const std::array inputs{PortType::event(subtype::Midi)};
       const std::vector outputs(node.voiceCount(), PortType::event(subtype::Midi));
       requirePorts(node, inputs, outputs);
       return NodeContent{};
     },
     [](Graph& g, NodeId id, const NodePorts& ports, const NodeContent& content) {
       requireEmpty(content);
       return g.restoreNode<VoiceAllocatorNode>(id, ports,
           static_cast<std::uint32_t>(ports.outputs.size()));
     }},
    {"modular.midi-block/1",
     [](const Node& n) { return typeid(n) == typeid(MidiBlockNode); },
     [](const Node& n) {
       const auto& node = static_cast<const MidiBlockNode&>(n);
       const std::array inputs{PortType::event(subtype::Trigger),
                               PortType::event(subtype::Trigger)};
       const std::array outputs{PortType::event(subtype::Midi)};
       requirePorts(node, inputs, outputs);
       validateBlock(node.entries());
       std::ostringstream text;
       text.imbue(std::locale::classic());
       text << node.entries().size() << '\n';
       for (const auto& entry : node.entries()) {
         text << entry.offsetFrames;
         writeMessage(text, entry.msg);
       }
       return storedText("", text.str());
     },
     [](Graph& g, NodeId id, const NodePorts& ports, const NodeContent& content) {
       if (!content.settings.empty())
         throw std::invalid_argument("open: unexpected midi block settings");
       ContentReader input(content);
       const auto count = input.count();
       std::vector<MidiBlockNode::Entry> entries;
       for (std::size_t i = 0; i < count; ++i) {
         const auto offset = input.number<std::uint64_t>();
         entries.push_back({offset, input.message()});
       }
       input.end();
       validateBlock(entries);
       return g.restoreNode<MidiBlockNode>(id, ports, std::move(entries));
     }},
    {"modular.midi-file/1",
     [](const Node& n) { return typeid(n) == typeid(MidiFileNode); },
     [](const Node& n) {
       const auto& node = static_cast<const MidiFileNode&>(n);
       const std::array inputs{PortType::event(subtype::Trigger),
                               PortType::event(subtype::Trigger)};
       const std::array outputs{PortType::event(subtype::Midi)};
       requirePorts(node, inputs, outputs);
       if (!node.ready()) return NodeContent{"empty", {}};
       const auto& file = node.file();
       std::ostringstream text;
       text.imbue(std::locale::classic());
       text << std::setprecision(std::numeric_limits<double>::max_digits10)
            << file.durationSeconds() << ' ' << file.skippedSysExEvents()
            << ' ' << file.entries().size() << '\n';
       for (const auto& entry : file.entries()) {
         text << entry.seconds;
         writeMessage(text, entry.message);
       }
       return storedText("loaded", text.str());
     },
     [](Graph& g, NodeId id, const NodePorts& ports, const NodeContent& content) {
       if (content.settings == "empty" && content.stored.empty())
         return g.restoreNode<MidiFileNode>(id, ports);
       if (content.settings != "loaded")
         throw std::invalid_argument("open: invalid midi file content state");
       ContentReader input(content);
       const auto duration = input.number<double>();
       const auto skipped = input.number<std::uint32_t>();
       const auto count = input.count();
       std::vector<MidiFile::Entry> entries;
       for (std::size_t i = 0; i < count; ++i) {
         const auto seconds = input.number<double>();
         entries.push_back({seconds, input.message()});
       }
       input.end();
       return g.restoreNode<MidiFileNode>(id, ports,
           MidiFile::fromEntries(std::move(entries), duration, skipped));
     }},
#ifdef _WIN32
    {"modular.windows-audio-output/1",
     [](const Node& n) { return typeid(n) == typeid(AudioOutputNode); },
     [](const Node& n) {
       const auto& node = static_cast<const AudioOutputNode&>(n);
       const std::array inputs{PortType::audio(node.channels())};
       requirePorts(node, inputs, {});
       return NodeContent{std::to_string(node.channels()), {}};
     },
     [](Graph& g, NodeId id, const NodePorts& ports, const NodeContent& content) {
       return g.restoreNode<AudioOutputNode>(id, ports, setting<std::uint32_t>(content));
     }},
#endif
  };
  return codecs;
}

std::span<const NodeCodec> soundFontCodecs() {
  static const NodeCodec codecs[]{
    {"modular.soundfont/1",
     [](const Node& n) { return typeid(n) == typeid(SoundFontNode); },
     [](const Node& n) {
       const auto& node = static_cast<const SoundFontNode&>(n);
       const std::array inputs{PortType::event(subtype::Midi)};
       const std::array outputs{PortType::audio(2)};
       requirePorts(node, inputs, outputs);
       std::ostringstream settings;
       settings.imbue(std::locale::classic());
       if (!node.ready()) {
         settings << "empty " << node.voiceLimit();
         return NodeContent{settings.str(), {}};
       }
       settings << "loaded " << node.bank() << ' ' << node.program() << ' '
                << node.voiceLimit();
       const auto bytes = std::as_bytes(node.soundFont().bytes());
       return NodeContent{settings.str(),
                          std::vector<std::byte>(bytes.begin(), bytes.end())};
     },
     [](Graph& g, NodeId id, const NodePorts& ports, const NodeContent& content) {
       const auto settings = soundFontSettings(content.settings);
       if (!settings.preset) {
         if (!content.stored.empty())
           throw std::invalid_argument("open: empty soundfont node has content");
         g.restoreNode<SoundFontNode>(id, ports);
       } else {
         std::vector<std::uint8_t> bytes(content.stored.size());
         std::transform(content.stored.begin(), content.stored.end(), bytes.begin(),
                        [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
         // The same parser as a load, so a file that no longer parses fails here.
         g.restoreNode<SoundFontNode>(id, ports, SoundFont::parse(bytes));
         static_cast<SoundFontNode&>(g.node(id))
             .setPreset(settings.preset->bank, settings.preset->program);
       }
       static_cast<SoundFontNode&>(g.node(id)).setVoiceLimit(settings.voiceLimit);
       return id;
     }},
  };
  return codecs;
}

} // namespace modular::nodes
