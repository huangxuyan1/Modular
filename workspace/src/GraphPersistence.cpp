#include "modular/GraphPersistence.h"

#include <array>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>

#include "modular/Supernode.h"

namespace modular {
namespace {

constexpr std::size_t kMaxFileBytes = 256 * 1024 * 1024;

void validateCodecs(std::span<const NodeCodec> codecs) {
  std::set<std::string> kinds;
  for (const auto& codec : codecs)
    if (codec.kind.empty() || !codec.matches || !codec.save || !codec.restore ||
        !kinds.insert(codec.kind).second)
      throw std::invalid_argument("persistence: invalid or duplicate node codec");
}

const NodeCodec& codecFor(const Node& node, std::span<const NodeCodec> codecs) {
  if (dynamic_cast<const Supernode*>(&node) || dynamic_cast<const BoundaryNode*>(&node))
    throw std::invalid_argument("save: supernodes and boundary nodes are unsupported");
  const NodeCodec* found = nullptr;
  for (const auto& codec : codecs) {
    if (!codec.matches(node)) continue;
    if (found) throw std::invalid_argument("save: ambiguous node codecs");
    found = &codec;
  }
  if (!found) throw std::invalid_argument("save: unsupported node kind; graph was not saved");
  return *found;
}

const NodeCodec& codecFor(const std::string& kind, std::span<const NodeCodec> codecs) {
  for (const auto& codec : codecs) if (codec.kind == kind) return codec;
  throw std::invalid_argument("open: unsupported node kind " + kind);
}

std::string hex(std::span<const std::byte> bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (const auto byte : bytes) {
    const auto value = std::to_integer<unsigned>(byte);
    result += digits[value >> 4];
    result += digits[value & 15];
  }
  return result;
}

std::vector<std::byte> unhex(const std::string& text) {
  if (text.size() % 2) throw std::invalid_argument("open: malformed stored content");
  const auto digit = [](char c) -> unsigned {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    throw std::invalid_argument("open: malformed stored content");
  };
  std::vector<std::byte> result;
  for (std::size_t i = 0; i < text.size(); i += 2)
    result.push_back(static_cast<std::byte>((digit(text[i]) << 4) | digit(text[i + 1])));
  return result;
}

// Strict token parsing: no signed-to-unsigned wrap, unquoted text, trailing
// junk, unbounded count allocation or host-locale dependence.
class Reader {
public:
  explicit Reader(std::string text) : size_(text.size()), input_(std::move(text)) {
    input_.imbue(std::locale::classic());
  }
  void tag(const char* expected) {
    std::string actual;
    if (!(input_ >> actual) || actual != expected)
      throw std::invalid_argument(std::string("open: expected ") + expected);
  }
  template <class T> T number() {
    std::string text;
    T value{};
    if (!(input_ >> text)) throw std::invalid_argument("open: missing number");
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
      throw std::invalid_argument("open: invalid number");
    return value;
  }
  std::uint32_t count() {
    const auto value = number<std::uint32_t>();
    if (value > size_) throw std::invalid_argument("open: count exceeds file size");
    return value;
  }
  std::string quoted() {
    std::string text;
    input_ >> std::ws;
    if (input_.peek() != '"' || !(input_ >> std::quoted(text)))
      throw std::invalid_argument("open: expected quoted text");
    return text;
  }
  void end() {
    tag("END");
    input_ >> std::ws;
    if (!input_.eof()) throw std::invalid_argument("open: trailing graph content");
  }
private:
  std::size_t size_;
  std::istringstream input_;
};

// Save writes the newest container version; Open reads every version so far.
// PORT is the only record that differs between them.
constexpr unsigned kWrittenVersion = 2;

// Version 1 stored one number per port: an audio port's channel count, or a
// value or event port's subtype. Version 2 stores the subtype, then the
// channel count, which is 0 for value and event ports.
std::vector<PortSpec> readPorts(Reader& input, const char* direction, unsigned version) {
  input.tag(direction);
  const auto count = input.count();
  std::vector<PortSpec> ports;
  for (std::uint32_t i = 0; i < count; ++i) {
    input.tag("PORT");
    const auto id = input.number<PortId>();
    const auto domain = input.number<unsigned>();
    if (domain > static_cast<unsigned>(Domain::Event))
      throw std::invalid_argument("open: unsupported port domain");
    PortType type{static_cast<Domain>(domain)};
    if (version == 1 && type.domain == Domain::Audio) {
      type.channels = input.number<std::uint32_t>();
    } else if (version == 1) {
      type.sub = input.number<std::uint32_t>();
    } else {
      type.sub = input.number<std::uint32_t>();
      type.channels = input.number<std::uint32_t>();
    }
    ports.push_back({id, type, input.quoted()});
  }
  return ports;
}

void writePorts(std::ostream& output, const char* direction,
                 std::span<const PortSpec> ports) {
  output << direction << ' ' << ports.size() << '\n';
  for (const auto& port : ports)
    output << "PORT " << port.id << ' ' << static_cast<unsigned>(port.type.domain)
           << ' ' << port.type.sub << ' ' << port.type.channels << ' '
           << std::quoted(port.name) << '\n';
}

} // namespace

void saveGraph(const Workspace& workspace, std::ostream& output,
                std::span<const NodeCodec> codecs) {
  validateCodecs(codecs);
  std::ostringstream text;
  text.imbue(std::locale::classic());
  text << std::setprecision(std::numeric_limits<double>::max_digits10);
  const auto& graph = workspace.graph();
  text << "MODULAR_GRAPH " << kWrittenVersion << "\nNEXT " << graph.nextIdentity()
       << "\nNODES " << graph.nodeCount() << '\n';
  for (const auto id : graph.nodeIds()) {
    const auto& node = graph.node(id);
    const auto& codec = codecFor(node, codecs);
    const auto content = codec.save(node);
    const auto position = workspace.position(id);
    text << "NODE " << id << ' ' << std::quoted(codec.kind)
         << "\nSETTINGS " << std::quoted(content.settings)
         << "\nCONTENT " << std::quoted(hex(content.stored))
         << "\nPOSITION " << position.x << ' ' << position.y << '\n';
    writePorts(text, "INPUTS", node.inputs());
    writePorts(text, "OUTPUTS", node.outputs());
  }
  text << "WIRES " << graph.connections().size() << '\n';
  for (const auto& wire : graph.connections())
    text << "WIRE " << wire.srcNode << ' ' << wire.srcPort << ' '
         << wire.dstNode << ' ' << wire.dstPort << '\n';
  text << "END\n";
  const auto encoded = text.str();
  if (encoded.size() > kMaxFileBytes)
    throw std::invalid_argument("save: graph exceeds 256 MiB format limit");
  output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
  if (!output) throw std::invalid_argument("save: could not write graph");
}

Workspace openGraph(std::istream& input, std::span<const NodeCodec> codecs) {
  validateCodecs(codecs);
  std::string bytes;
  std::array<char, 4096> chunk{};
  while (input.read(chunk.data(), chunk.size()) || input.gcount()) {
    if (bytes.size() + input.gcount() > kMaxFileBytes)
      throw std::invalid_argument("open: graph exceeds 256 MiB format limit");
    bytes.append(chunk.data(), static_cast<std::size_t>(input.gcount()));
  }
  if (input.bad() || !input.eof()) throw std::invalid_argument("open: could not read graph");
  Reader reader(std::move(bytes));
  reader.tag("MODULAR_GRAPH");
  const auto version = reader.number<unsigned>();
  if (version < 1 || version > kWrittenVersion)
    throw std::invalid_argument("open: unsupported graph version");
  reader.tag("NEXT");
  Workspace workspace;
  workspace.graph() = Graph(reader.number<std::uint64_t>());
  reader.tag("NODES");
  const auto count = reader.count();
  for (std::uint32_t i = 0; i < count; ++i) {
    reader.tag("NODE");
    const auto id = reader.number<NodeId>();
    const auto& codec = codecFor(reader.quoted(), codecs);
    reader.tag("SETTINGS");
    NodeContent content{reader.quoted(), {}};
    reader.tag("CONTENT");
    content.stored = unhex(reader.quoted());
    reader.tag("POSITION");
    const NodePosition position{reader.number<double>(), reader.number<double>()};
    const NodePorts ports{readPorts(reader, "INPUTS", version),
                          readPorts(reader, "OUTPUTS", version)};
    if (codec.restore(workspace.graph(), id, ports, content) != id)
      throw std::invalid_argument("open: node codec changed identity");
    workspace.setPosition(id, position);
  }
  reader.tag("WIRES");
  const auto wires = reader.count();
  for (std::uint32_t i = 0; i < wires; ++i) {
    reader.tag("WIRE");
    const auto srcNode = reader.number<NodeId>();
    const auto srcPort = reader.number<PortId>();
    const auto dstNode = reader.number<NodeId>();
    const auto dstPort = reader.number<PortId>();
    workspace.graph().connect(srcNode, srcPort, dstNode, dstPort);
  }
  reader.end();
  return workspace;
}

} // namespace modular
