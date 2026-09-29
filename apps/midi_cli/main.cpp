#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "modular/Engine.h"
#include "modular/nodes/BasicNodes.h"
#include "modular/nodes/MidiFileNode.h"
#include "modular/nodes/WavFile.h"
#ifdef _WIN32
#include "modular/nodes/AudioOutputNode.h"
#endif

using namespace modular;
using namespace modular::nodes;

// This is an example application graph, not policy inside either new node.
// It deliberately reuses the existing eight-voice sine instrument.
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
  if (argc < 3 || argc > 4 ||
      (argc == 4 && std::filesystem::path(argv[3]) != "--play")) {
    std::cerr << "usage: midi_cli input.mid output.wav [--play]\n"
                 "Renders up to ten minutes using eight sine voices.\n"
                 "--play also sends the graph's audio to the Windows default device.\n";
    return 1;
  }
  try {
    const bool play = argc == 4;
    const std::filesystem::path input(argv[1]), output(argv[2]);
    if (std::filesystem::exists(output))
      throw std::invalid_argument("midi cli: output already exists; choose a new file name");
#ifndef _WIN32
    if (play) throw std::invalid_argument("midi cli: speaker output currently requires Windows");
#endif
    auto file = MidiFile::read(input);
    if (file.durationSeconds() > 600)
      throw std::invalid_argument("midi cli: this in-memory demo is limited to ten minutes");
    std::cout << "Eight sine voices; program changes, controllers and percussion sounds "
                 "are not interpreted by this instrument.\n";
    if (file.skippedSysExEvents() != 0)
      std::cout << "Skipped " << file.skippedSysExEvents() << " SysEx events.\n";

    constexpr double kRate = 48000;
    constexpr std::uint32_t kBlock = 512;
    constexpr std::uint32_t kVoices = 8;
    Graph graph;
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
#ifdef _WIN32
    AudioOutputNode* speakers = nullptr;
    if (play) {
      const auto speakersId = graph.addNode<AudioOutputNode>(1);
      speakers = &static_cast<AudioOutputNode&>(graph.node(speakersId));
      graph.connect(gainId, gain.out(), speakersId, speakers->in());
    }
#endif
    Engine engine(graph, kRate, kBlock);
    // Dispatch events exactly at the file's end, too. EOT duration includes silence.
    const auto total = midi.durationFrames() + 1;
    while (engine.streamTime() < total) {
      engine.process(static_cast<std::uint32_t>(
          std::min<std::uint64_t>(kBlock, total - engine.streamTime())));
#ifdef _WIN32
      if (speakers && speakers->failed()) throw std::runtime_error(speakers->errorMessage());
#endif
    }
#ifdef _WIN32
    if (speakers && !speakers->drain()) throw std::runtime_error(speakers->errorMessage());
#endif
    writeWav16(output, capture.samples(), capture.channels(), static_cast<std::uint32_t>(kRate));
    // Narrowing a Windows path through the current ANSI code page can throw,
    // even after a successful render. Diagnostics use UTF-8, too.
    const auto outputText = output.u8string();
    std::cout << (play ? "Played and rendered " : "Rendered ") << total << " frames -> ";
    std::cout.write(reinterpret_cast<const char*>(outputText.data()),
                    static_cast<std::streamsize>(outputText.size()));
    std::cout << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
