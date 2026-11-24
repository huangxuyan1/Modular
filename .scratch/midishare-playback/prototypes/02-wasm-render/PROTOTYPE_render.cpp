// PROTOTYPE, throwaway: midishare-playback ticket 02, the WebAssembly offline render probe.
//
// apps/midi_cli's graph (trigger -> MIDI file -> voice allocator -> eight sine voices -> mix
// -> gain -> capture), taking Standard MIDI File bytes and a sample rate instead of a path,
// and keeping the samples in memory instead of writing a WAV file. Built twice:
//
// - with em++ (build.ps1): a module that exports the C functions below to JavaScript;
// - with MSVC (build.ps1): a native reference, `render_native in.mid rate out.f32`, that
//   writes the same float samples raw, so the two can be compared bit for bit.
//
// The sine voice stands in for the soundfont node and is never shipped.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "modular/Engine.h"
#include "modular/nodes/BasicNodes.h"
#include "modular/nodes/MidiFileNode.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define PROBE_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define PROBE_EXPORT extern "C"
#endif

using namespace modular;
using namespace modular::nodes;

namespace {

// The last render. The graph stays alive so its capture buffer can be read without a copy.
std::unique_ptr<Graph> g_graph;
const std::vector<float>* g_samples = nullptr;
std::string g_error;

// The body of apps/midi_cli/main.cpp, as of origin/main 86a8d12, minus the path, the WAV and
// the speakers. Returns the frame count.
std::uint64_t renderMidi(std::span<const std::uint8_t> bytes, double rate) {
  g_samples = nullptr;
  g_graph.reset();
  auto file = MidiFile::parse(bytes);
  if (file.durationSeconds() > 600)
    throw std::invalid_argument("midi cli: this in-memory demo is limited to ten minutes");

  constexpr std::uint32_t kBlock = 512;
  constexpr std::uint32_t kVoices = 8;
  auto owned = std::make_unique<Graph>();
  Graph& graph = *owned;
  const auto startId = graph.addNode<TriggerAtNode>(0);
  const auto fileId = graph.addNode<MidiFileNode>(std::move(file));
  const auto voicesId = graph.addNode<VoiceAllocatorNode>(kVoices);
  const auto mixId = graph.addNode<MixNode>(kVoices, 1);
  const auto levelId = graph.addNode<ConstFloatNode>(0.15f);
  const auto gainId = graph.addNode<GainNode>(1);
  const auto captureId = graph.addNode<CaptureSinkNode>(1);
  auto& start = static_cast<TriggerAtNode&>(graph.node(startId));
  auto& midi = static_cast<MidiFileNode&>(graph.node(fileId));
  auto& voices = static_cast<VoiceAllocatorNode&>(graph.node(voicesId));
  auto& mix = static_cast<MixNode&>(graph.node(mixId));
  auto& level = static_cast<ConstFloatNode&>(graph.node(levelId));
  auto& gain = static_cast<GainNode&>(graph.node(gainId));
  auto& capture = static_cast<CaptureSinkNode&>(graph.node(captureId));
  graph.connect(startId, start.trigger(), fileId, midi.trigger());
  graph.connect(fileId, midi.midi(), voicesId, voices.midiIn());
  for (std::uint32_t voice = 0; voice < kVoices; ++voice) {
    const auto synthId = graph.addNode<SineSynthNode>();
    auto& synth = static_cast<SineSynthNode&>(graph.node(synthId));
    graph.connect(voicesId, voices.voice(voice), synthId, synth.midiIn());
    graph.connect(synthId, synth.out(), mixId, mix.in(voice));
  }
  graph.connect(mixId, mix.out(), gainId, gain.in());
  graph.connect(levelId, level.value(), gainId, gain.gain());
  graph.connect(gainId, gain.out(), captureId, capture.in());

  {
    Engine engine(graph, rate, kBlock);
    // Dispatch events exactly at the file's end, too. EOT duration includes silence.
    const auto total = midi.durationFrames() + 1;
    while (engine.streamTime() < total)
      engine.process(static_cast<std::uint32_t>(
          std::min<std::uint64_t>(kBlock, total - engine.streamTime())));
  }
  g_graph = std::move(owned);
  g_samples = &capture.samples();
  return g_samples->size();
}

// A kernel authoring error: an audio output wired to an event input.
void kernelAuthoringError() {
  Graph graph;
  const auto synthId = graph.addNode<SineSynthNode>();
  const auto otherId = graph.addNode<SineSynthNode>();
  auto& synth = static_cast<SineSynthNode&>(graph.node(synthId));
  auto& other = static_cast<SineSynthNode&>(graph.node(otherId));
  graph.connect(synthId, synth.out(), otherId, other.midiIn());
}

} // namespace

// Renders; an authoring error is caught inside the module. Returns the frame count, or -1
// with the message in modular_error().
PROBE_EXPORT int modular_render(const std::uint8_t* bytes, int size, double rate) {
  try {
    g_error.clear();
    return static_cast<int>(renderMidi({bytes, static_cast<std::size_t>(size)}, rate));
  } catch (const std::exception& error) {
    g_error = error.what();
    return -1;
  }
}

// The same, with no catch: an authoring error leaves the module as a C++ exception.
PROBE_EXPORT int modular_render_uncaught(const std::uint8_t* bytes, int size, double rate) {
  return static_cast<int>(renderMidi({bytes, static_cast<std::size_t>(size)}, rate));
}

PROBE_EXPORT int modular_kernel_error_caught() {
  try {
    kernelAuthoringError();
    return 0;
  } catch (const std::exception& error) {
    g_error = error.what();
    return -1;
  }
}

PROBE_EXPORT void modular_kernel_error_uncaught() { kernelAuthoringError(); }

PROBE_EXPORT const float* modular_samples() { return g_samples ? g_samples->data() : nullptr; }
PROBE_EXPORT const char* modular_error() { return g_error.c_str(); }
PROBE_EXPORT void modular_release() {
  g_samples = nullptr;
  g_graph.reset();
}

#ifndef __EMSCRIPTEN__
#include <chrono>

int main(int argc, char** argv) {
  if (argc != 4) {
    std::fprintf(stderr, "usage: render_native in.mid rate out.f32\n");
    return 2;
  }
  std::FILE* in = std::fopen(argv[1], "rb");
  if (!in) return 3;
  std::vector<std::uint8_t> bytes;
  for (int c; (c = std::fgetc(in)) != EOF;) bytes.push_back(static_cast<std::uint8_t>(c));
  std::fclose(in);
  const auto began = std::chrono::steady_clock::now();
  const int frames = modular_render(bytes.data(), static_cast<int>(bytes.size()),
                                    std::stod(argv[2]));
  const std::chrono::duration<double, std::milli> took = std::chrono::steady_clock::now() - began;
  if (frames < 0) {
    std::fprintf(stderr, "%s\n", modular_error());
    return 1;
  }
  std::FILE* out = std::fopen(argv[3], "wb");
  if (!out) return 3;
  std::fwrite(modular_samples(), sizeof(float), static_cast<std::size_t>(frames), out);
  std::fclose(out);
  std::printf("{\"frames\": %d, \"renderMs\": %.3f}\n", frames, took.count());
  return 0;
}
#endif
