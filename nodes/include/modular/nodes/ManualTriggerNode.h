#pragma once

#include <atomic>

#include "modular/Node.h"
#include "Payloads.h"

namespace modular::nodes {

// Start/Stop are authored labels for the same trigger source. Only its wires
// determine what a press does. Multiple presses before a block coalesce.
class ManualTriggerNode : public Node {
public:
  enum class Label { Start, Stop };
  explicit ManualTriggerNode(Graph& graph, Label label = Label::Start)
      : Node(graph), label_(label) {
    trigger_ = addOutput(PortType::event(subtype::Trigger), "trigger");
  }

  Label label() const { return label_; }
  PortId trigger() const { return trigger_; }
  void press() noexcept { pending_.store(true); } // safe alongside process()
  void prepare(const PrepareInfo&) override { pending_.store(false); }
  void process(ProcessContext& ctx) override {
    if (ctx.numFrames() != 0 && pending_.exchange(false))
      ctx.eventsOut(trigger_).push(Event::make(0, TriggerPayload{}));
  }

private:
  Label label_;
  PortId trigger_;
  std::atomic<bool> pending_{false};
};

} // namespace modular::nodes
