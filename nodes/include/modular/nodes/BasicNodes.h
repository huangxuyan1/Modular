#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "modular/Graph.h"
#include "modular/Node.h"
#include "Payloads.h"

// The medium-agnostic half of the node library: sources, level, summing, and
// a sink. Nothing here knows what it is carrying — grep this file for "midi"
// and you get nothing, one layer up from the same rule about the kernel. A
// speech or transcription pipeline should be buildable from this file alone;
// anything that knows what a note is lives in MidiNodes.h.
//
// TriggerAtNode stands in for a transport's start signal, and ConstFloatNode
// for a value someone can drag; a real transport and a real control surface
// are just richer sets of nodes like these.
//
// Every node here takes the graph it is built into as its first constructor
// parameter (graph.addNode<GainNode>(1)), keeps the port ids that come back
// in its own members, and names those members again in process(). A port's
// position never appears.

namespace modular::nodes {

// --- sources -------------------------------------------------------------

// Emits one trigger event when stream time crosses a configured frame.
class TriggerAtNode : public Node {
public:
  TriggerAtNode(Graph& graph, std::uint64_t atFrame) : Node(graph), atFrame_(atFrame) {
    trigger_ = addOutput(PortType::event(subtype::Trigger), "trigger");
  }

  PortId trigger() const { return trigger_; }

  void process(ProcessContext& ctx) override {
    const std::uint64_t start = ctx.blockStartFrame();
    const std::uint64_t end = start + ctx.numFrames();
    if (atFrame_ >= start && atFrame_ < end) {
      ctx.eventsOut(trigger_)
          .push(Event::make(static_cast<std::uint32_t>(atFrame_ - start),
                            TriggerPayload{}));
    }
  }

private:
  PortId trigger_ = kNoPort;
  std::uint64_t atFrame_;
};

// Authored constant, editable only when no Engine borrows the graph.
// This setter is not a thread-safe runtime input.
class ConstFloatNode : public Node {
public:
  ConstFloatNode(Graph& graph, float v) : Node(graph), v_(v) {
    setConstant(v);
    value_ = addOutput(PortType::value(subtype::Float), "value");
  }

  PortId value() const { return value_; }

  float constant() const { return v_; }
  void setConstant(float value) {
    if (!std::isfinite(value))
      throw std::invalid_argument("constant: value must be finite");
    v_ = value;
  }

  void process(ProcessContext& ctx) override { ctx.valueOut(value_).set(v_); }

private:
  PortId value_ = kNoPort;
  float v_;
};

// --- processors ----------------------------------------------------------

// out = in * gain. Gain is a value input, read through the valueAt() contract.
class GainNode : public Node {
public:
  GainNode(Graph& graph, std::uint32_t channels) : Node(graph), channels_(channels) {
    if (channels == 0) throw std::invalid_argument("gain: needs at least one channel");
    in_ = addInput(PortType::audio(channels), "in");
    gain_ = addInput(PortType::value(subtype::Float), "gain");
    out_ = addOutput(PortType::audio(channels), "out");
  }

  // The node's two audio-and-a-control ports told apart by name rather than by
  // declaration order: whoever wires this asks for `gain()`, not for input 1.
  PortId in() const { return in_; }
  PortId gain() const { return gain_; }
  PortId out() const { return out_; }

  std::uint32_t channels() const { return channels_; }
  // Edit-only configuration. A wired control input is unaffected by this edit.
  void setChannels(Graph& graph, NodeId self, std::uint32_t channels) {
    if (channels == 0) throw std::invalid_argument("gain: needs at least one channel");
    const std::array changes{PortTypeChange{in_, PortType::audio(channels)},
                             PortTypeChange{out_, PortType::audio(channels)}};
    setPortTypes(graph, self, changes);
    channels_ = channels;
  }

  void process(ProcessContext& ctx) override {
    const auto in = ctx.audioIn(in_);
    const auto out = ctx.audioOut(out_);
    const float g = ctx.valueIn(gain_).valueAt<float>(0);
    for (std::uint32_t c = 0; c < channels_; ++c)
      for (std::uint32_t i = 0; i < ctx.numFrames(); ++i)
        out.channels[c][i] = in.channels[c][i] * g;
  }

private:
  PortId in_ = kNoPort, gain_ = kNoPort, out_ = kNoPort;
  std::uint32_t channels_;
};

// Sums audio inputs into one. The graph rejects fan-in — exactly one
// connection per input — because merging needs a policy, and holding one
// would force the kernel to interpret what it carries. This node *is* that
// policy for audio: plain summation, no scaling, since level is a gain's job.
//
// Its inputs are a homogeneous set, which is the one place order still means
// something: in(3) is the fourth input this node declared, and it resolves to
// an id. That ordering lives in the node's own vector, not in the graph.
// Summation follows it, so the result does not depend on block size.
class MixNode : public Node {
public:
  MixNode(Graph& graph, std::uint32_t inputCount, std::uint32_t channels)
      : Node(graph), channels_(channels) {
    if (inputCount == 0) throw std::invalid_argument("mix: needs at least one input");
    if (channels == 0) throw std::invalid_argument("mix: needs at least one channel");
    for (std::uint32_t i = 0; i < inputCount; ++i)
      ins_.push_back(addInput(PortType::audio(channels), "in" + std::to_string(i)));
    out_ = addOutput(PortType::audio(channels), "out");
  }

  PortId in(std::uint32_t index) const { return ins_[index]; }
  PortId out() const { return out_; }

  std::uint32_t inputCount() const { return static_cast<std::uint32_t>(ins_.size()); }
  std::uint32_t channels() const { return channels_; }

  // Edit-only. Shrinking removes trailing inputs under RESTRICT; growing mints
  // new ids. Validate the whole request before changing arity or channel count.
  void configure(Graph& graph, NodeId self, std::uint32_t count,
                 std::uint32_t channels) {
    if (&graph.node(self) != this)
      throw std::invalid_argument("configure: node does not belong to this graph and id");
    if (count == 0) throw std::invalid_argument("mix: needs at least one input");
    if (channels == 0) throw std::invalid_argument("mix: needs at least one channel");
    if (count > ins_.size()) requirePortCapacity(count - ins_.size());
    for (std::size_t i = count; i < ins_.size(); ++i)
      if (!graph.connectionsOf(self, ins_[i]).empty())
        throw std::invalid_argument("mix: input is still wired; disconnect first");
    std::vector<PortTypeChange> changes{{out_, PortType::audio(channels)}};
    for (const auto port : ins_) changes.push_back({port, PortType::audio(channels)});
    ins_.reserve(count);
    setPortTypes(graph, self, changes);
    while (ins_.size() > count) graph.removePort(self, ins_.back());
    while (ins_.size() < count)
      ins_.push_back(addInput(PortType::audio(channels),
                              "in" + std::to_string(ins_.size())));
    channels_ = channels;
  }

  void process(ProcessContext& ctx) override {
    const auto out = ctx.audioOut(out_);
    const std::uint32_t frames = ctx.numFrames();
    for (std::uint32_t c = 0; c < channels_; ++c)
      for (std::uint32_t i = 0; i < frames; ++i) out.channels[c][i] = 0.0f;
    for (const PortId port : ins_) {
      const auto in = ctx.audioIn(port);
      for (std::uint32_t c = 0; c < channels_; ++c)
        for (std::uint32_t i = 0; i < frames; ++i) out.channels[c][i] += in.channels[c][i];
    }
  }

private:
  void portRemoved(PortId port) override { std::erase(ins_, port); }

  std::vector<PortId> ins_;
  PortId out_ = kNoPort;
  std::uint32_t channels_;
};

// --- sinks ---------------------------------------------------------------

// Accumulates its audio input into memory (interleaved). A test/offline sink.
class CaptureSinkNode : public Node {
public:
  CaptureSinkNode(Graph& graph, std::uint32_t channels)
      : Node(graph), channels_(channels) {
    in_ = addInput(PortType::audio(channels), "in");
  }

  PortId in() const { return in_; }

  void process(ProcessContext& ctx) override {
    const auto in = ctx.audioIn(in_);
    for (std::uint32_t i = 0; i < ctx.numFrames(); ++i)
      for (std::uint32_t c = 0; c < channels_; ++c)
        samples_.push_back(in.channels[c][i]);
  }

  std::uint32_t channels() const { return channels_; }
  const std::vector<float>& samples() const { return samples_; }

private:
  PortId in_ = kNoPort;
  std::uint32_t channels_;
  std::vector<float> samples_; // interleaved
};

} // namespace modular::nodes
