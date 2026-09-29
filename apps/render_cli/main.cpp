// Milestone 1: prove all three domains in one graph, offline, no UI, no JUCE.
//
//   [trigger @0.1s] --event<trigger>--> [midi block] --event<midi>--> [synth]
//                                                                       |
//   [const 0.5] --value<float>--> [gain] <--audio<1>---------------------
//                                   |
//                              [capture sink] --> out.wav

#include <cstdio>
#include <memory>

#include "modular/Engine.h"
#include "modular/Graph.h"
#include "modular/nodes/BasicNodes.h"
#include "modular/nodes/MidiNodes.h"
#include "modular/nodes/WavFile.h"

using namespace modular;
using namespace modular::nodes;

int main(int argc, char** argv) {
  const char* outPath = argc > 1 ? argv[1] : "out.wav";
  constexpr double kSampleRate = 48000.0;
  constexpr std::uint32_t kBlock = 512;
  constexpr std::uint64_t kTotalFrames = 3 * 48000; // 3 seconds

  // A little arpeggio: C4 E4 G4 C5, quarter notes at 120bpm (0.5s = 24000 frames).
  auto on = [](std::uint8_t note) { return MidiMsg{0x90, note, 100}; };
  auto off = [](std::uint8_t note) { return MidiMsg{0x80, note, 0}; };
  std::vector<MidiBlockNode::Entry> clip;
  const std::uint8_t notes[] = {60, 64, 67, 72};
  for (int i = 0; i < 4; ++i) {
    const std::uint64_t start = static_cast<std::uint64_t>(i) * 24000;
    clip.push_back({start, on(notes[i])});
    clip.push_back({start + 22000, off(notes[i])});
  }

  // Each node is built into the graph, which mints its port ids; wiring then
  // names ports through the accessors the nodes publish, never by position.
  Graph graph;
  const NodeId triggerId = graph.addNode<TriggerAtNode>(4800); // 0.1s
  const NodeId blockId = graph.addNode<MidiBlockNode>(clip);
  const NodeId synthId = graph.addNode<SineSynthNode>();
  const NodeId gainValId = graph.addNode<ConstFloatNode>(0.5f);
  const NodeId gainId = graph.addNode<GainNode>(1);
  const NodeId sinkId = graph.addNode<CaptureSinkNode>(1);

  auto& trigger = static_cast<TriggerAtNode&>(graph.node(triggerId));
  auto& block = static_cast<MidiBlockNode&>(graph.node(blockId));
  auto& synth = static_cast<SineSynthNode&>(graph.node(synthId));
  auto& gainVal = static_cast<ConstFloatNode&>(graph.node(gainValId));
  auto& gain = static_cast<GainNode&>(graph.node(gainId));
  auto& sink = static_cast<CaptureSinkNode&>(graph.node(sinkId));

  graph.connect(triggerId, trigger.trigger(), blockId, block.trigger());
  graph.connect(blockId, block.midi(), synthId, synth.midiIn());
  graph.connect(synthId, synth.out(), gainId, gain.in());
  graph.connect(gainValId, gainVal.value(), gainId, gain.gain());
  graph.connect(gainId, gain.out(), sinkId, sink.in());

  Engine engine(graph, kSampleRate, kBlock);
  while (engine.streamTime() < kTotalFrames) {
    const std::uint64_t remaining = kTotalFrames - engine.streamTime();
    engine.process(remaining < kBlock ? static_cast<std::uint32_t>(remaining) : kBlock);
  }

  writeWav16(outPath, sink.samples(), sink.channels(),
             static_cast<std::uint32_t>(kSampleRate));
  std::printf("rendered %llu frames -> %s\n",
              static_cast<unsigned long long>(engine.streamTime()), outPath);
  return 0;
}
