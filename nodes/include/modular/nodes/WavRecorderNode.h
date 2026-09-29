#pragma once

#include <filesystem>
#include <atomic>
#include <mutex>
#include <functional>
#include <string>
#include <vector>

#include "modular/Node.h"
#include "WavFile.h"

namespace modular::nodes {

// Node-owned capture: Start includes its sample, Stop excludes its sample and
// wins ties. Duplicate Start/Stop are harmless. Engine release aborts capture.
class WavRecorderNode : public Node {
public:
  enum class State { Idle, Recording, Writing, Failed };
  // External file boundary: write and close the supplied temporary file, or
  // throw. The recorder owns publication, collision handling and cleanup.
  using Writer = std::function<void(const std::filesystem::path&,
      const std::vector<float>&, std::uint32_t, std::uint32_t)>;
  WavRecorderNode(Graph&, std::filesystem::path directory = ".",
                  std::string prefix = "take", std::uint32_t channels = 1,
                  Writer writer = writeWav16);
  PortId in() const { return in_; }
  PortId start() const { return start_; }
  PortId stop() const { return stop_; }
  const std::filesystem::path& directory() const { return directory_; }
  const std::string& prefix() const { return prefix_; }
  std::uint32_t channels() const { return channels_; }
  // Edit-only. Validate wires and all settings before changing authored state.
  void configure(Graph&, NodeId, std::filesystem::path, std::string,
                  std::uint32_t channels);
  State state() const { return state_.load(); }
  std::string statusText() const;
  // Thread-safe requests, consumed before the next block. Only failed takes
  // accept these controls; neither operation is a signal or an authored edit.
  void retry() noexcept;
  void discard() noexcept;
  void prepare(const PrepareInfo&) override;
  void process(ProcessContext&) override;
  void release() noexcept override;

private:
  enum class Request { None, Retry, Discard };
  void completeTake();
  void publish(State, std::string);
  void writeTake();
  PortId in_, start_, stop_;
  std::filesystem::path directory_;
  std::string prefix_;
  std::uint32_t channels_, sample_rate_ = 0;
  std::atomic<State> state_{State::Idle};
  std::atomic<Request> request_{Request::None};
  mutable std::mutex status_mutex_;
  std::string status_ = "Idle";
  std::vector<float> samples_;
  Writer writer_;
};

} // namespace modular::nodes
