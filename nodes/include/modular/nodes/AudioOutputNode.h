#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "modular/Node.h"

namespace modular::nodes {

// Windows default-device sink. Buffered playback applies backpressure to the
// processing thread: run the Engine on an application worker, not the GUI thread
// or a hard-realtime callback. No device vocabulary reaches the kernel.
class AudioOutputNode : public Node {
public:
  explicit AudioOutputNode(Graph& graph, std::uint32_t channels = 1);
  ~AudioOutputNode() override;

  PortId in() const { return in_; }
  void prepare(const PrepareInfo& info) override;
  void release() noexcept override;
  void process(ProcessContext& ctx) override;

  // Call on the processing thread after the last block to let queued audio finish.
  // Destruction instead discards queued audio and closes the device.
  bool drain();
  bool failed() const { return error_.load() != 0; }
  std::string errorMessage() const override; // may allocate; never call from process()
  std::uint32_t channels() const { return channels_; }
  void setChannels(Graph& graph, NodeId self, std::uint32_t channels);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  PortId in_;
  std::uint32_t channels_;
  std::atomic<std::uint32_t> error_{0};
};

} // namespace modular::nodes
