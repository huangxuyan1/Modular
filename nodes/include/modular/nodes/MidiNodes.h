#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "modular/Node.h"
#include "modular/Graph.h"
#include "Payloads.h"

// Nodes that know what a note is. Everything here reads or writes
// event<midi>, so a pipeline with no music in it — transcription, podcast
// assembly, analysis — has no reason to include this file. The generic
// plumbing those pipelines do want lives in BasicNodes.h, which names no
// medium at all.
//
// The kernel, one layer further down, knows about neither.

namespace modular::nodes {

// A stored MIDI stream that fires from the top when triggered. Content
// offsets are sorted frames relative to Start. Stop cancels the sequence;
// retrigger and completion release held channel/note identities. Bare blocks
// end at their last entry (empty blocks at zero); files supply their EOT time.
class MidiBlockNode : public Node {
public:
  struct Entry {
    std::uint64_t offsetFrames; // relative to trigger
    MidiMsg msg;
  };

  MidiBlockNode(Graph& graph, std::vector<Entry> content)
      : Node(graph), content_(std::move(content)) {
    for (const auto& entry : content_)
      if ((entry.msg.isNoteOn() || entry.msg.isNoteOff()) && entry.msg.data1 >= 128)
        throw std::invalid_argument("midi block: note number must be below 128");
    trigger_ = addInput(PortType::event(subtype::Trigger), "start");
    stop_ = addInput(PortType::event(subtype::Trigger), "stop");
    midi_ = addOutput(PortType::event(subtype::Midi), "midi");
    duration_frames_ = content_.empty() ? 0 : content_.back().offsetFrames;
  }

  PortId trigger() const { return trigger_; }
  PortId stop() const { return stop_; }
  PortId midi() const { return midi_; }
  // Frame-domain block content. File nodes resolve this during prepare; their
  // authored content instead lives in MidiFileNode::file().
  const std::vector<Entry>& entries() const { return content_; }

  void prepare(const PrepareInfo&) override {
    cursor_ = 0;
    playing_ = false;
    held_ = {};
  }

  void process(ProcessContext& ctx) override {
    const std::uint64_t blockStart = ctx.blockStartFrame();
    const std::uint64_t blockEnd = blockStart + ctx.numFrames();
    auto& out = ctx.eventsOut(midi_);

    // Advance up to each trigger before restarting. Processing all triggers
    // first would erase earlier events in the same block, making block size
    // change the music. A restart replaces pending events at its exact frame.
    const auto emitBefore = [&](std::uint64_t end) {
      if (!playing_) return;
      while (cursor_ < content_.size()) {
        const auto offset = content_[cursor_].offsetFrames;
        if (offset >= end - startFrame_) break;
        const auto due = startFrame_ + offset;
        if (due >= blockStart) {
          out.push(Event::make(static_cast<std::uint32_t>(due - blockStart),
                               content_[cursor_].msg));
          remember(content_[cursor_].msg);
        }
        ++cursor_;
      }
      if (duration_frames_ < end - startFrame_) {
        releaseHeld(out, static_cast<std::uint32_t>(startFrame_ + duration_frames_ - blockStart));
        playing_ = false;
      }
    };

    auto start = ctx.eventsIn(trigger_).begin();
    const auto startsEnd = ctx.eventsIn(trigger_).end();
    auto stop = ctx.eventsIn(stop_).begin();
    const auto stopsEnd = ctx.eventsIn(stop_).end();
    while (start != startsEnd || stop != stopsEnd) {
      const auto offset = start == startsEnd ? stop->offset : stop == stopsEnd
          ? start->offset : std::min(start->offset, stop->offset);
      emitBefore(blockStart + offset);
      releaseHeld(out, offset);
      const bool stopping = stop != stopsEnd && stop->offset == offset;
      // Consume the entire shared instant. Stop suppresses every Start at it.
      while (start != startsEnd && start->offset == offset) ++start;
      while (stop != stopsEnd && stop->offset == offset) ++stop;
      startFrame_ = blockStart + offset;
      cursor_ = 0;
      playing_ = !stopping;
    }

    emitBefore(blockEnd);
  }

protected:
  // A file-backed subclass resolves its timestamps at prepare time. Replacing
  // content is an authoring/prepare operation, never a live process mutation.
  void replaceContent(std::vector<Entry> content, std::uint64_t durationFrames) {
    content_ = std::move(content);
    duration_frames_ = durationFrames;
    MidiBlockNode::prepare({});
  }

private:
  void remember(MidiMsg message) {
    if (message.isNoteOn() || message.isNoteOff())
      held_[message.status & 15][message.data1] = message.isNoteOn();
  }

  void releaseHeld(EventBuffer& out, std::uint32_t offset) {
    for (std::size_t channel = 0; channel < held_.size(); ++channel)
      for (std::size_t note = 0; note < held_[channel].size(); ++note)
        if (held_[channel][note]) {
          out.push(Event::make(offset, MidiMsg{
              static_cast<std::uint8_t>(0x80 | channel),
              static_cast<std::uint8_t>(note), 0}));
          held_[channel][note] = false;
        }
  }

  std::array<std::array<bool, 128>, 16> held_{};
  PortId trigger_ = kNoPort, stop_ = kNoPort, midi_ = kNoPort;
  std::uint64_t duration_frames_ = 0;
  std::vector<Entry> content_; // sorted by offsetFrames
  std::uint64_t startFrame_ = 0;
  std::size_t cursor_ = 0;
  bool playing_ = false;
};

// Splits one MIDI stream into N monophonic streams, so a stack of mono synths
// can play a chord. A note-on takes a free voice — lowest index first — or
// steals the longest-held one; a note-off follows its note to whichever voice
// holds it, and is dropped if that note was already stolen.
//
// Voice allocation is node wiring, not a kernel concept: a different policy
// (round-robin, release-priority, per-channel) is simply a different node.
// State is carried across blocks, but it advances only with the event stream,
// so allocation is identical at any block size.
//
// Its outputs are a homogeneous set: voice(2) is the third one declared, and
// the ordering lives in this node's own vector of port ids.
class VoiceAllocatorNode : public Node {
public:
  VoiceAllocatorNode(Graph& graph, std::uint32_t voices) : Node(graph) {
    if (voices == 0) throw std::invalid_argument("voice allocator: needs at least one voice");
    midiIn_ = addInput(PortType::event(subtype::Midi), "midi");
    for (std::uint32_t v = 0; v < voices; ++v)
      voices_.push_back(addOutput(PortType::event(subtype::Midi),
                                  "voice" + std::to_string(v)));
    held_.assign(voices, kFree);
    age_.assign(voices, 0);
  }

  PortId midiIn() const { return midiIn_; }
  PortId voice(std::uint32_t index) const { return voices_[index]; }
  std::uint32_t voiceCount() const { return static_cast<std::uint32_t>(voices_.size()); }

  void setVoiceCount(Graph& graph, NodeId self, std::uint32_t count) {
    if (&graph.node(self) != this)
      throw std::invalid_argument("voice allocator: wrong graph or node id");
    if (count == 0) throw std::invalid_argument("voice allocator: needs at least one voice");
    if (count > voices_.size()) requirePortCapacity(count - voices_.size());
    for (std::size_t i = count; i < voices_.size(); ++i)
      if (!graph.connectionsOf(self, voices_[i]).empty())
        throw std::invalid_argument("voice allocator: voice is still wired; disconnect first");
    voices_.reserve(count);
    held_.resize(count, kFree);
    age_.resize(count, 0);
    while (voices_.size() > count) graph.removePort(self, voices_.back());
    while (voices_.size() < count)
      voices_.push_back(addOutput(PortType::event(subtype::Midi),
                                  "voice" + std::to_string(voices_.size())));
  }

  void prepare(const PrepareInfo&) override {
    if (voices_.empty())
      throw std::invalid_argument("voice allocator: needs at least one voice");
    // Graph edits may remove any unwired output. Rebuild execution state from
    // the surviving ports, rather than retaining the constructor's voice count.
    held_.assign(voices_.size(), kFree);
    age_.assign(voices_.size(), 0);
    clock_ = 0;
  }

  void process(ProcessContext& ctx) override {
    for (const Event& e : ctx.eventsIn(midiIn_)) {
      const MidiMsg m = e.as<MidiMsg>();
      if (m.isNoteOn()) {
        ctx.eventsOut(voices_[assign(m.data1)]).push(e); // offset preserved: same Event
      } else if (m.isNoteOff()) {
        const std::size_t v = release(m.data1);
        if (v != kNone) ctx.eventsOut(voices_[v]).push(e);
      }
      // Anything else is not a note; this node allocates voices, nothing more.
    }
  }

private:
  static constexpr int kFree = -1;
  void portRemoved(PortId port) override { std::erase(voices_, port); }
  static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

  std::size_t assign(std::uint8_t note) {
    for (std::size_t v = 0; v < held_.size(); ++v) // retrigger keeps its voice
      if (held_[v] == note) return touch(v, note);
    for (std::size_t v = 0; v < held_.size(); ++v)
      if (held_[v] == kFree) return touch(v, note);
    std::size_t oldest = 0; // all busy: steal the one held longest
    for (std::size_t v = 1; v < held_.size(); ++v)
      if (age_[v] < age_[oldest]) oldest = v;
    return touch(oldest, note);
  }

  std::size_t touch(std::size_t v, std::uint8_t note) {
    held_[v] = note;
    age_[v] = ++clock_;
    return v;
  }

  std::size_t release(std::uint8_t note) {
    for (std::size_t v = 0; v < held_.size(); ++v)
      if (held_[v] == note) {
        held_[v] = kFree;
        return v;
      }
    return kNone;
  }

  PortId midiIn_ = kNoPort;
  std::vector<PortId> voices_;     // one output per voice, in voice order
  std::vector<int> held_;          // note number per voice, or kFree
  std::vector<std::uint64_t> age_; // assignment order, for stealing
  std::uint64_t clock_ = 0;
};

// Monophonic sine synth, sample-accurate note starts. Last-note priority,
// no envelope (a hard on/off keeps timing observable for tests). Polyphony is
// a VoiceAllocatorNode, several of these, and a MixNode — not a bigger synth.
class SineSynthNode : public Node {
public:
  explicit SineSynthNode(Graph& graph) : Node(graph) {
    midiIn_ = addInput(PortType::event(subtype::Midi), "midi");
    out_ = addOutput(PortType::audio(1), "out");
  }

  PortId midiIn() const { return midiIn_; }
  PortId out() const { return out_; }

  void prepare(const PrepareInfo& info) override {
    sampleRate_ = info.sampleRate;
    phase_ = phaseInc_ = 0;
    note_ = -1;
    active_ = false;
  }

  void process(ProcessContext& ctx) override {
    float* out = ctx.audioOut(out_).channels[0];
    const Event* ev = ctx.eventsIn(midiIn_).begin();
    const Event* evEnd = ctx.eventsIn(midiIn_).end();

    std::uint32_t pos = 0;
    while (pos < ctx.numFrames()) {
      while (ev != evEnd && ev->offset == pos) handle(ev++->as<MidiMsg>());
      const std::uint32_t segEnd =
          (ev != evEnd) ? std::min(ev->offset, ctx.numFrames()) : ctx.numFrames();
      for (std::uint32_t i = pos; i < segEnd; ++i) {
        out[i] = active_ ? 0.5f * static_cast<float>(std::sin(phase_)) : 0.0f;
        if (active_) phase_ += phaseInc_;
      }
      pos = segEnd;
    }
  }

private:
  void handle(const MidiMsg& m) {
    if (m.isNoteOn()) {
      note_ = m.data1;
      const double freq = 440.0 * std::pow(2.0, (note_ - 69) / 12.0);
      phaseInc_ = 2.0 * 3.14159265358979323846 * freq / sampleRate_;
      phase_ = 0.0;
      active_ = true;
    } else if (m.isNoteOff() && m.data1 == note_) {
      active_ = false;
    }
  }

  PortId midiIn_ = kNoPort, out_ = kNoPort;
  double sampleRate_ = 48000.0;
  double phase_ = 0.0, phaseInc_ = 0.0;
  int note_ = -1;
  bool active_ = false;
};

} // namespace modular::nodes
