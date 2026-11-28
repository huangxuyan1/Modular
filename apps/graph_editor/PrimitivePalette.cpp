#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "PrimitivePalette.h"

#include <stdexcept>

#include "modular/nodes/BasicNodes.h"
#include "modular/nodes/ManualTriggerNode.h"
#include "modular/nodes/MidiFileNode.h"
#include "modular/nodes/AudioOutputNode.h"
#include "modular/nodes/WavRecorderNode.h"

namespace modular::editor {
using namespace nodes;

namespace {
CreationSettings noSettings(const Node&) { return {}; }
void applyNoSettings(Graph&, NodeId, const CreationSettings&) {}
}

const std::array<PrimitivePresentation, kPaletteSize>& palette() {
  static const std::array<PrimitivePresentation, kPaletteSize> entries{{
    {L"ConstFloatNode",
     L"An authored constant. Value changes apply in Edit.\n"
     L"Output: value<float>. This is not a live control.",
     true, false, false,
     [](const Node& n) { return dynamic_cast<const ConstFloatNode*>(&n) != nullptr; },
     [](Graph& g, const CreationSettings& s) { return g.addNode<ConstFloatNode>(s.constant); },
     [](const Node& n) {
       CreationSettings s;
       s.constant = static_cast<const ConstFloatNode&>(n).constant();
       return s;
     },
     [](Graph& g, NodeId id, const CreationSettings& s) {
       static_cast<ConstFloatNode&>(g.node(id)).setConstant(s.constant);
     }},
    {L"GainNode",
     L"Channels are Edit configuration.\n"
     L"Gain is a value<float> input: wire a constant to it. Unwired gain reads zero.",
     false, true, false,
     [](const Node& n) { return dynamic_cast<const GainNode*>(&n) != nullptr; },
     [](Graph& g, const CreationSettings& s) { return g.addNode<GainNode>(s.channels); },
     [](const Node& n) {
       CreationSettings s;
       s.channels = static_cast<const GainNode&>(n).channels();
       return s;
     },
     [](Graph& g, NodeId id, const CreationSettings& s) {
       static_cast<GainNode&>(g.node(id)).setChannels(g, id, s.channels);
     }},
    {L"MixNode",
     L"Sums its audio inputs. Channels and input count are Edit configuration.\n"
     L"Disconnect trailing inputs before removing them.",
     false, true, true,
     [](const Node& n) { return dynamic_cast<const MixNode*>(&n) != nullptr; },
     [](Graph& g, const CreationSettings& s) {
       return g.addNode<MixNode>(s.inputs, s.channels);
     },
     [](const Node& n) {
       const auto& mix = static_cast<const MixNode&>(n);
       CreationSettings s;
       s.channels = mix.channels();
       s.inputs = mix.inputCount();
       return s;
     },
     [](Graph& g, NodeId id, const CreationSettings& s) {
       static_cast<MixNode&>(g.node(id)).configure(g, id, s.inputs, s.channels);
     }},
    {L"Start",
     L"Manual trigger source. In Run, select this placed node and press Start. "
     L"It emits at the next processing block. Repeated presses before a block coalesce. "
     L"Only its wires decide what starts.",
     false, false, false,
     [](const Node& n) { const auto* t = dynamic_cast<const ManualTriggerNode*>(&n);
       return t && t->label() == ManualTriggerNode::Label::Start; },
     [](Graph& g, const CreationSettings&) { return g.addNode<ManualTriggerNode>(); },
     noSettings, applyNoSettings},
    {L"Stop",
     L"Manual trigger source. In Run, select this placed node and press Stop. "
     L"Wire its output to the intended Stop inputs. This is not a global transport.",
     false, false, false,
     [](const Node& n) { const auto* t = dynamic_cast<const ManualTriggerNode*>(&n);
       return t && t->label() == ManualTriggerNode::Label::Stop; },
     [](Graph& g, const CreationSettings&) {
       return g.addNode<ManualTriggerNode>(ManualTriggerNode::Label::Stop); },
     noSettings, applyNoSettings},
    {L"MidiFileNode",
     L"Load a Standard MIDI File (format 0/1, PPQ, tempo changes). Format 2 and "
     L"SMPTE timing are unsupported; SysEx is skipped. Content belongs to this node. "
     L"Start restarts and releases held notes; Stop cancels future events and releases "
     L"notes. Stop wins ties. Completion releases notes. Downstream nodes may ignore "
     L"non-note messages. Failed replacement keeps previous content.",
     false, false, false,
     [](const Node& n) { return dynamic_cast<const MidiFileNode*>(&n) != nullptr; },
     [](Graph& g, const CreationSettings&) { return g.addNode<MidiFileNode>(); },
     noSettings, applyNoSettings},
    {L"SineSynthNode",
     L"Mono, monophonic sine; last-note priority, fixed amplitude, hard on/off. "
     L"No velocity dynamics, envelopes, controllers, pitch bend, program changes "
     L"or percussion. For polyphony wire allocator voices to separate synths, "
     L"then wire those into a mixer.",
     false, false, false,
     [](const Node& n) { return dynamic_cast<const SineSynthNode*>(&n) != nullptr; },
     [](Graph& g, const CreationSettings&) { return g.addNode<SineSynthNode>(); },
     noSettings, applyNoSettings},
    {L"VoiceAllocatorNode",
     L"Splits notes into the configured voices. Lowest free voice first, then "
     L"steals the oldest. Note identity is note number, ignoring MIDI channel. "
     L"Non-note messages are not forwarded. Wire each voice to a separate synth, "
     L"then sum their audio with a mixer. Disconnect trailing voices before shrinking.",
     false, false, true,
     [](const Node& n) { return dynamic_cast<const VoiceAllocatorNode*>(&n) != nullptr; },
     [](Graph& g, const CreationSettings& s) { return g.addNode<VoiceAllocatorNode>(s.inputs); },
     [](const Node& n) { CreationSettings s;
       s.inputs = static_cast<const VoiceAllocatorNode&>(n).voiceCount(); return s; },
     [](Graph& g, NodeId id, const CreationSettings& s) {
       static_cast<VoiceAllocatorNode&>(g.node(id)).setVoiceCount(g, id, s.inputs); }},
    {L"AudioOutputNode",
     L"Windows default speakers/headphones. Mono or stereo input; choose 1 or 2 "
     L"channels in Edit. Buffered playback, not a low-latency driver. Readiness is "
     L"checked on Run. If opening fails, repair the Windows default output device "
     L"and try Run again. Leaving Run discards queued audio and closes the device.",
     false, true, false,
     [](const Node& n) { return dynamic_cast<const AudioOutputNode*>(&n) != nullptr; },
     [](Graph& g, const CreationSettings& s) { return g.addNode<AudioOutputNode>(s.channels); },
     [](const Node& n) { CreationSettings s;
       s.channels = static_cast<const AudioOutputNode&>(n).channels(); return s; },
     [](Graph& g, NodeId id, const CreationSettings& s) {
       static_cast<AudioOutputNode&>(g.node(id)).setChannels(g, id, s.channels); }},
    {L"WavRecorderNode",
     L"Start records; Stop saves a new WAV.\r\n"
     L"Leaving Run discards unfinished takes.",
     false, true, false,
     [](const Node& n) { return dynamic_cast<const WavRecorderNode*>(&n) != nullptr; },
     [](Graph& g, const CreationSettings& s) {
       return g.addNode<WavRecorderNode>(s.directory, s.prefix, s.channels); },
     [](const Node& n) {
       const auto& recorder = static_cast<const WavRecorderNode&>(n);
       CreationSettings s;
       s.channels = recorder.channels(); s.directory = recorder.directory();
       s.prefix = recorder.prefix(); return s;
     },
     [](Graph& g, NodeId id, const CreationSettings& s) {
       static_cast<WavRecorderNode&>(g.node(id)).configure(
           g, id, s.directory, s.prefix, s.channels); }, true}
  }};
  return entries;
}

const PrimitivePresentation& presentation(const Node& node) {
  for (const auto& entry : palette()) if (entry.matches(node)) return entry;
  // Other clients can author frame-based blocks. Opening them does not require
  // adding an event editor or an empty-block placement control to this palette.
  static const PrimitivePresentation block{L"MidiBlockNode",
      L"Stored MIDI with frame offsets relative to Start. Start restarts; Stop and "
      L"completion release held notes. Stop wins ties. This app can wire, move and "
      L"save the block; editing its messages requires another Modular client.",
      false, false, false,
      [](const Node& n) { return typeid(n) == typeid(MidiBlockNode); },
      nullptr, noSettings, applyNoSettings};
  if (block.matches(node)) return block;
  throw std::invalid_argument("palette: unsupported primitive node");
}

std::wstring portTypeName(PortType type) {
  // No placeable node declares an audio subtype, so the label shows width alone.
  if (type.domain == Domain::Audio)
    return L"audio<" + std::to_wstring(type.channels) + L">";
  const auto sub = type.sub == subtype::Float && type.domain == Domain::Value
      ? L"float" : type.domain == Domain::Event && type.sub == subtype::Trigger
      ? L"trigger" : type.domain == Domain::Event && type.sub == subtype::Midi
      ? L"midi" : std::to_wstring(type.sub);
  return (type.domain == Domain::Value ? L"value<" : L"event<") + sub + L">";
}

std::wstring widen(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                      static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring result(size, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                      result.data(), size);
  return result;
}

} // namespace modular::editor
