#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "Graph.h"

namespace modular {

// v1 engine: single-threaded, compiles the graph in the constructor
// (topological order + buffer allocation), then processes block by block.
// No delay compensation yet, therefore no cycles: a cyclic graph throws
// std::invalid_argument at compile time.
//
// The engine's only clock is stream time: a monotonic count of frames
// dispatched. It knows nothing about timelines, transport, or tempo.
class Engine {
public:
  // Compiles the graph. The graph must outlive the engine and must not be
  // edited while the engine exists (editing = build a new Engine).
  Engine(Graph& graph, double sampleRate, std::uint32_t maxBlockFrames);
  ~Engine();

  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Process the next numFrames of stream time (numFrames <= maxBlockFrames).
  // Block size may vary call to call — results must not depend on it.
  void process(std::uint32_t numFrames);

  std::uint64_t streamTime() const { return streamTime_; }
  std::string errorMessage() const; // same thread as process(), between blocks

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::uint64_t streamTime_ = 0;
  std::uint32_t maxBlockFrames_ = 0;
};

} // namespace modular
