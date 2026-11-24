#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "modular/Graph.h"
#include "modular/Node.h"

namespace modular::web {

// Two planar channels of equal length, left and right.
struct Channels {
  std::vector<float> left, right;
};

// The render core's sink: two planar channels, sized before each render.
// Unlike CaptureSinkNode it never grows in process(), and it does not keep a
// previous Engine's samples, so a kept graph renders clip after clip.
class StereoSinkNode : public Node {
public:
  explicit StereoSinkNode(Graph& graph) : Node(graph) {
    in_ = addInput(PortType::audio(2), "in");
  }

  PortId in() const { return in_; }

  // Sizes both channels to hold `frames`, before an Engine processes a block:
  // the only place the sink allocates. Frames past the capacity are dropped.
  void setCapacity(std::size_t frames) {
    left_.assign(frames, 0.0f);
    right_.assign(frames, 0.0f);
  }

  // A fresh Engine writes from the first frame again.
  void prepare(const PrepareInfo&) override { written_ = 0; }

  void process(ProcessContext& ctx) override {
    const auto in = ctx.audioIn(in_);
    const std::size_t frames =
        std::min<std::size_t>(ctx.numFrames(), left_.size() - written_);
    std::copy_n(in.channels[0], frames, left_.data() + written_);
    std::copy_n(in.channels[1], frames, right_.data() + written_);
    written_ += frames;
  }

  // Moves out the frames written since the last prepare, leaving the sink empty.
  Channels take() {
    left_.resize(written_);
    right_.resize(written_);
    Channels channels{std::move(left_), std::move(right_)};
    left_.clear();
    right_.clear();
    written_ = 0;
    return channels;
  }

private:
  PortId in_ = kNoPort;
  std::vector<float> left_, right_;
  std::size_t written_ = 0;
};

} // namespace modular::web
