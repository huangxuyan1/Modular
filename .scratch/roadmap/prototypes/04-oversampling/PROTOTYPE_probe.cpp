// PROTOTYPE — throwaway probe for .scratch/roadmap/issues/04-rate-probe-oversampling.md
//
// Question: can a 4x-oversampled distortion be expressed in today's model —
// upsample node, nonlinearity, downsample node, wired in a graph, rendered
// offline — or does sample rate have to enter the port type or the block
// contract?
//
// This is a spike, not a design. It compiles the *shipped* kernel sources and
// uses only what exists today: Graph, Node, Engine, and GainNode,
// ConstFloatNode and CaptureSinkNode from nodes/. Every node defined below is
// user space; nothing here needed a kernel change.
//
// The yardstick is a plain whole-signal reference that oversamples the
// textbook way (zero-stuff, filter at 4x, shape at 4x, filter, keep every 4th
// sample) on ordinary arrays, outside any graph. A graph "expresses" 4x
// oversampling if its render equals that reference sample for sample, at
// every block size.
//
// Build/run: ./run.ps1

#include "modular/Engine.h"
#include "modular/Graph.h"
#include "modular/Node.h"
#include "modular/nodes/BasicNodes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <numbers>
#include <vector>

using namespace modular;
using modular::nodes::CaptureSinkNode;
using modular::nodes::ConstFloatNode;
using modular::nodes::GainNode;

namespace {

constexpr double kRate = 48000.0;
constexpr std::uint32_t kFactor = 4;
constexpr std::uint32_t kMaxBlock = 512;
constexpr std::uint32_t kTotal = 16000;
constexpr double kFreq = 5010.0; // an exact bin of the 4800-point DFT below
constexpr double kAmp = 0.8;
constexpr float kDrive = 4.0f;   // memoryless stage: tanh(drive * x)
constexpr float kFbDrive = 2.0f; // stateful stage: y = tanh(drive * x + fb * y)
constexpr float kFb = 0.5f;

constexpr double kPi = std::numbers::pi;

float sine(std::uint64_t frame) {
  return static_cast<float>(kAmp * std::sin(2.0 * kPi * kFreq * double(frame) / kRate));
}

// ---------------------------------------------------------------------------
// One windowed-sinc lowpass at the 4x rate, shared by interpolation and
// decimation. 257 taps: 128 samples of group delay at 4x is 32 base frames
// per filter, so the pair's latency (64) is a whole number of base frames and
// latencySamples() can state it.
// ---------------------------------------------------------------------------

constexpr int kTaps = 257;

struct Filters {
  std::vector<float> down; // unity DC gain
  std::vector<float> up;   // x4, making up for the zeros stuffed between samples
};

const Filters& filters() {
  static const Filters f = [] {
    Filters r;
    const double fc = 20000.0 / (kRate * kFactor);
    std::vector<double> h(kTaps);
    double sum = 0.0;
    for (int n = 0; n < kTaps; ++n) {
      const double m = n - (kTaps - 1) / 2.0;
      const double sinc = m == 0 ? 2.0 * fc : std::sin(2.0 * kPi * fc * m) / (kPi * m);
      const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * n / (kTaps - 1)) +
                       0.08 * std::cos(4.0 * kPi * n / (kTaps - 1));
      h[n] = sinc * w;
      sum += h[n];
    }
    for (double v : h) {
      r.down.push_back(static_cast<float>(v / sum));
      r.up.push_back(static_cast<float>(kFactor * v / sum));
    }
    return r;
  }();
  return f;
}

// The two nonlinearities. Memoryless is what a textbook distortion is;
// stateful is the stress case, since its state must step through the 4x
// samples in time order.
struct Tanh {
  float operator()(float x) const { return std::tanh(x); }
};
struct FeedbackTanh {
  float drive = kFbDrive, fb = kFb, y = 0.0f;
  float operator()(float x) {
    y = std::tanh(drive * x + fb * y);
    return y;
  }
};

// ---------------------------------------------------------------------------
// The reference: plain arrays, no graph, no blocks.
// ---------------------------------------------------------------------------

template <class Shape>
std::vector<float> reference4x(float preGain, Shape shape) {
  const Filters& f = filters();
  std::vector<float> stuffed(std::size_t(kTotal) * kFactor, 0.0f);
  for (std::uint32_t m = 0; m < kTotal; ++m) stuffed[std::size_t(m) * kFactor] = sine(m);

  std::vector<float> shaped(stuffed.size());
  for (std::size_t n = 0; n < stuffed.size(); ++n) {
    float acc = 0.0f;
    for (int k = 0; k < kTaps && std::size_t(k) <= n; ++k) acc += f.up[k] * stuffed[n - k];
    shaped[n] = shape(acc * preGain);
  }

  std::vector<float> y(kTotal);
  for (std::uint32_t m = 0; m < kTotal; ++m) {
    const std::size_t n = std::size_t(m) * kFactor;
    float acc = 0.0f;
    for (int k = 0; k < kTaps && std::size_t(k) <= n; ++k) acc += f.down[k] * shaped[n - k];
    y[m] = acc;
  }
  return y;
}

// ---------------------------------------------------------------------------
// Polyphase codec. A 4x signal is carried at the base rate as 4 channels:
// channel p at frame m holds 4x-sample 4m + p. Both halves keep their filter
// history as node state, so the result cannot depend on block size.
// ---------------------------------------------------------------------------

struct History {
  std::vector<float> buf;
  std::size_t pos = 0;
  explicit History(std::size_t n) : buf(n, 0.0f) {}
  void push(float v) {
    pos = (pos + 1) % buf.size();
    buf[pos] = v;
  }
  float back(std::size_t k) const { return buf[(pos + buf.size() - k) % buf.size()]; }
};

struct Interpolator {
  History hist{(kTaps + kFactor - 1) / kFactor};
  void run(float x, float* out4) {
    const Filters& f = filters();
    hist.push(x);
    for (std::uint32_t p = 0; p < kFactor; ++p) {
      float acc = 0.0f;
      for (std::size_t j = 0; j * kFactor + p < std::size_t(kTaps); ++j)
        acc += f.up[j * kFactor + p] * hist.back(j);
      out4[p] = acc;
    }
  }
};

struct Decimator {
  History hist{kTaps};
  float run(const float* in4) {
    const Filters& f = filters();
    hist.push(in4[0]);
    float acc = 0.0f;
    for (int k = 0; k < kTaps; ++k) acc += f.down[k] * hist.back(k);
    for (std::uint32_t p = 1; p < kFactor; ++p) hist.push(in4[p]);
    return acc;
  }
};

// ---------------------------------------------------------------------------
// User-space nodes.
// ---------------------------------------------------------------------------

// A test tone computed from stream time, so it is block-size invariant.
class SineNode : public Node {
public:
  explicit SineNode(Graph& g) : Node(g) { out_ = addOutput(PortType::audio(1), "out"); }
  PortId out() const { return out_; }
  void process(ProcessContext& ctx) override {
    const auto o = ctx.audioOut(out_);
    for (std::uint32_t i = 0; i < ctx.numFrames(); ++i)
      o.channels[0][i] = sine(ctx.blockStartFrame() + i);
  }

private:
  PortId out_ = kNoPort;
};

// audio<1> at the base rate -> audio<4>, the polyphase form of the 4x signal.
class Up4Node : public Node {
public:
  explicit Up4Node(Graph& g) : Node(g) {
    in_ = addInput(PortType::audio(1), "in");
    out_ = addOutput(PortType::audio(kFactor), "out4x");
  }
  PortId in() const { return in_; }
  PortId out() const { return out_; }
  std::uint32_t latencySamples() const override { return (kTaps - 1) / 2 / kFactor; }
  void process(ProcessContext& ctx) override {
    const auto in = ctx.audioIn(in_);
    const auto out = ctx.audioOut(out_);
    for (std::uint32_t i = 0; i < ctx.numFrames(); ++i) {
      float o[kFactor];
      interp_.run(in.channels[0][i], o);
      for (std::uint32_t p = 0; p < kFactor; ++p) out.channels[p][i] = o[p];
    }
  }

private:
  PortId in_ = kNoPort, out_ = kNoPort;
  Interpolator interp_;
};

// audio<4> polyphase -> audio<1> at the base rate.
class Down4Node : public Node {
public:
  explicit Down4Node(Graph& g) : Node(g) {
    in_ = addInput(PortType::audio(kFactor), "in4x");
    out_ = addOutput(PortType::audio(1), "out");
  }
  PortId in() const { return in_; }
  PortId out() const { return out_; }
  std::uint32_t latencySamples() const override { return (kTaps - 1) / 2 / kFactor; }
  void process(ProcessContext& ctx) override {
    const auto in = ctx.audioIn(in_);
    const auto out = ctx.audioOut(out_);
    for (std::uint32_t i = 0; i < ctx.numFrames(); ++i) {
      float v[kFactor];
      for (std::uint32_t p = 0; p < kFactor; ++p) v[p] = in.channels[p][i];
      out.channels[0][i] = dec_.run(v);
    }
  }

private:
  PortId in_ = kNoPort, out_ = kNoPort;
  Decimator dec_;
};

// Written the way any library node is: per channel, knows nothing about what
// the channels mean. Memoryless, so it neither knows nor cares about rate.
class TanhNode : public Node {
public:
  TanhNode(Graph& g, std::uint32_t channels) : Node(g), channels_(channels) {
    in_ = addInput(PortType::audio(channels), "in");
    out_ = addOutput(PortType::audio(channels), "out");
  }
  PortId in() const { return in_; }
  PortId out() const { return out_; }
  void process(ProcessContext& ctx) override {
    const auto in = ctx.audioIn(in_);
    const auto out = ctx.audioOut(out_);
    for (std::uint32_t c = 0; c < channels_; ++c)
      for (std::uint32_t i = 0; i < ctx.numFrames(); ++i)
        out.channels[c][i] = Tanh{}(in.channels[c][i]);
  }

private:
  PortId in_ = kNoPort, out_ = kNoPort;
  std::uint32_t channels_;
};

// The stateful nonlinearity written as a stock library node: one state per
// channel, each channel walked in time order. Correct at any single rate.
class StockFeedbackTanhNode : public Node {
public:
  StockFeedbackTanhNode(Graph& g, std::uint32_t channels)
      : Node(g), channels_(channels), state_(channels) {
    in_ = addInput(PortType::audio(channels), "in");
    out_ = addOutput(PortType::audio(channels), "out");
  }
  PortId in() const { return in_; }
  PortId out() const { return out_; }
  double preparedRate = 0.0;
  void prepare(const PrepareInfo& info) override { preparedRate = info.sampleRate; }
  void process(ProcessContext& ctx) override {
    const auto in = ctx.audioIn(in_);
    const auto out = ctx.audioOut(out_);
    for (std::uint32_t c = 0; c < channels_; ++c)
      for (std::uint32_t i = 0; i < ctx.numFrames(); ++i)
        out.channels[c][i] = state_[c](in.channels[c][i]);
  }

private:
  PortId in_ = kNoPort, out_ = kNoPort;
  std::uint32_t channels_;
  std::vector<FeedbackTanh> state_;
};

// The same nonlinearity written for the polyphase form: one state for the one
// signal, stepped through the 4 phases of each frame in time order.
class PhaseAwareFeedbackTanhNode : public Node {
public:
  explicit PhaseAwareFeedbackTanhNode(Graph& g) : Node(g) {
    in_ = addInput(PortType::audio(kFactor), "in4x");
    out_ = addOutput(PortType::audio(kFactor), "out4x");
  }
  PortId in() const { return in_; }
  PortId out() const { return out_; }
  double preparedRate = 0.0;
  void prepare(const PrepareInfo& info) override { preparedRate = info.sampleRate; }
  void process(ProcessContext& ctx) override {
    const auto in = ctx.audioIn(in_);
    const auto out = ctx.audioOut(out_);
    for (std::uint32_t i = 0; i < ctx.numFrames(); ++i)
      for (std::uint32_t p = 0; p < kFactor; ++p)
        out.channels[p][i] = state_(in.channels[p][i]);
  }

private:
  PortId in_ = kNoPort, out_ = kNoPort;
  FeedbackTanh state_;
};

// --- Part 1 helpers: the literal reading, rate as buffer length -------------

class RateAsLengthUpNode : public Node {
public:
  explicit RateAsLengthUpNode(Graph& g) : Node(g) {
    in_ = addInput(PortType::audio(1), "in");
    out_ = addOutput(PortType::audio(1), "out");
  }
  PortId in() const { return in_; }
  PortId out() const { return out_; }
  std::uint32_t wanted = 0, offered = 0;
  void process(ProcessContext& ctx) override {
    const auto out = ctx.audioOut(out_);
    wanted = ctx.numFrames() * kFactor; // what a 4x signal of this block needs
    offered = out.numFrames;            // what the view lets it write
    for (std::uint32_t i = 0; i < out.numFrames; ++i) out.channels[0][i] = 0.0f;
  }

private:
  PortId in_ = kNoPort, out_ = kNoPort;
};

class FrameProbeNode : public Node {
public:
  explicit FrameProbeNode(Graph& g) : Node(g) { in_ = addInput(PortType::audio(1), "in"); }
  PortId in() const { return in_; }
  std::uint32_t told = 0, viewed = 0;
  void process(ProcessContext& ctx) override {
    told = ctx.numFrames();
    viewed = ctx.audioIn(in_).numFrames;
  }

private:
  PortId in_ = kNoPort;
};

// --- Part 4 helpers: a node that owns a 4x-rate Engine ----------------------

class InjectNode : public Node {
public:
  explicit InjectNode(Graph& g) : Node(g) { out_ = addOutput(PortType::audio(1), "out"); }
  PortId out() const { return out_; }
  const float* src = nullptr;
  void process(ProcessContext& ctx) override {
    const auto o = ctx.audioOut(out_);
    std::copy(src, src + ctx.numFrames(), o.channels[0]);
  }

private:
  PortId out_ = kNoPort;
};

class CollectNode : public Node {
public:
  explicit CollectNode(Graph& g) : Node(g) { in_ = addInput(PortType::audio(1), "in"); }
  PortId in() const { return in_; }
  float* dst = nullptr;
  void process(ProcessContext& ctx) override {
    const auto i = ctx.audioIn(in_);
    std::copy(i.channels[0], i.channels[0] + ctx.numFrames(), dst);
  }

private:
  PortId in_ = kNoPort;
};

// audio<1> -> audio<1>. Inside, an ordinary graph of ordinary nodes runs at
// 4x under its own Engine, which hands them 4x the sample rate in prepare()
// and 4x the frames in process(). The outer compile sees one opaque node.
class Oversampled4xNode : public Node {
public:
  explicit Oversampled4xNode(Graph& outer) : Node(outer) {
    in_ = addInput(PortType::audio(1), "in");
    out_ = addOutput(PortType::audio(1), "out");
    inject_ = inner_.addNode<InjectNode>();
    shaper_ = inner_.addNode<StockFeedbackTanhNode>(1); // the stock node, unmodified
    collect_ = inner_.addNode<CollectNode>();
    inner_.connect(inject_, injectNode().out(), shaper_, shaperNode().in());
    inner_.connect(shaper_, shaperNode().out(), collect_, collectNode().in());
  }
  PortId in() const { return in_; }
  PortId out() const { return out_; }
  StockFeedbackTanhNode& shaperNode() {
    return static_cast<StockFeedbackTanhNode&>(inner_.node(shaper_));
  }
  std::uint32_t latencySamples() const override { return (kTaps - 1) / kFactor; }

  void prepare(const PrepareInfo& info) override {
    upBuf_.assign(std::size_t(info.maxBlockFrames) * kFactor, 0.0f);
    downBuf_.assign(std::size_t(info.maxBlockFrames) * kFactor, 0.0f);
    injectNode().src = upBuf_.data();
    collectNode().dst = downBuf_.data();
    engine_ = std::make_unique<Engine>(inner_, info.sampleRate * kFactor,
                                       info.maxBlockFrames * kFactor);
  }
  void release() noexcept override { engine_.reset(); }

  void process(ProcessContext& ctx) override {
    const auto in = ctx.audioIn(in_);
    const auto out = ctx.audioOut(out_);
    const std::uint32_t n = ctx.numFrames();
    for (std::uint32_t i = 0; i < n; ++i)
      interp_.run(in.channels[0][i], &upBuf_[std::size_t(i) * kFactor]);
    engine_->process(n * kFactor);
    for (std::uint32_t i = 0; i < n; ++i)
      out.channels[0][i] = dec_.run(&downBuf_[std::size_t(i) * kFactor]);
  }

private:
  InjectNode& injectNode() { return static_cast<InjectNode&>(inner_.node(inject_)); }
  CollectNode& collectNode() { return static_cast<CollectNode&>(inner_.node(collect_)); }

  PortId in_ = kNoPort, out_ = kNoPort;
  Graph inner_; // declared before engine_, so the Engine dies first
  NodeId inject_{}, shaper_{}, collect_{};
  std::unique_ptr<Engine> engine_;
  std::vector<float> upBuf_, downBuf_;
  Interpolator interp_;
  Decimator dec_;
};

// ---------------------------------------------------------------------------
// Harness.
// ---------------------------------------------------------------------------

using Plan = std::vector<std::uint32_t>;

Plan constantBlocks(std::uint32_t b) {
  Plan p;
  for (std::uint32_t t = 0; t < kTotal; t += b) p.push_back(std::min(b, kTotal - t));
  return p;
}

Plan randomBlocks() {
  Plan p;
  std::uint32_t s = 12345, t = 0;
  while (t < kTotal) {
    s = s * 1664525u + 1013904223u;
    const std::uint32_t b = std::min(1 + (s >> 8) % kMaxBlock, kTotal - t);
    p.push_back(b);
    t += b;
  }
  return p;
}

struct Plans {
  const char* name;
  Plan plan;
};

std::vector<Plans> allPlans() {
  return {{"1", constantBlocks(1)},     {"7", constantBlocks(7)},
          {"64", constantBlocks(64)},   {"480", constantBlocks(480)},
          {"512", constantBlocks(512)}, {"random 1..512", randomBlocks()}};
}

// Builds a fresh graph (nodes hold state), renders it offline, returns the sink.
template <class Build>
std::vector<float> render(Build build, const Plan& plan) {
  Graph g;
  const NodeId sink = build(g);
  {
    Engine e(g, kRate, kMaxBlock);
    for (std::uint32_t b : plan) e.process(b);
  }
  return static_cast<CaptureSinkNode&>(g.node(sink)).samples();
}

struct Diff {
  std::size_t equal = 0;
  double maxAbs = 0.0;
};

Diff compare(const std::vector<float>& a, const std::vector<float>& b) {
  Diff d;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i] == b[i]) ++d.equal;
    d.maxAbs = std::max(d.maxAbs, std::fabs(double(a[i]) - double(b[i])));
  }
  return d;
}

// Energy on bins that are not harmonics of kFreq, relative to energy on the
// harmonics, over an exactly periodic window: aliasing, in dB.
double aliasDb(const std::vector<float>& y) {
  constexpr std::size_t L = 4800, start = 9600;
  const std::size_t f0 = std::size_t(kFreq * L / kRate);
  std::vector<double> c(L), s(L);
  for (std::size_t k = 0; k < L; ++k) {
    c[k] = std::cos(2.0 * kPi * double(k) / L);
    s[k] = std::sin(2.0 * kPi * double(k) / L);
  }
  double harmonic = 0.0, alias = 0.0;
  for (std::size_t b = 1; b < L / 2; ++b) {
    double re = 0.0, im = 0.0;
    for (std::size_t n = 0; n < L; ++n) {
      re += y[start + n] * c[(b * n) % L];
      im -= y[start + n] * s[(b * n) % L];
    }
    (b % f0 == 0 ? harmonic : alias) += re * re + im * im;
  }
  return 10.0 * std::log10(alias / harmonic);
}

void printDiffs(const char* label, const std::vector<float>& ref, auto build) {
  std::printf("  %s\n", label);
  for (const Plans& p : allPlans()) {
    const Diff d = compare(render(build, p.plan), ref);
    std::printf("    blocks %-14s %5zu/%u samples == reference   max |diff| %.3g\n",
                p.name, d.equal, kTotal, d.maxAbs);
  }
}

// --- graph builders --------------------------------------------------------

NodeId polyphaseChain(Graph& g, int stage) {
  const NodeId src = g.addNode<SineNode>();
  const NodeId up = g.addNode<Up4Node>();
  const NodeId down = g.addNode<Down4Node>();
  const NodeId sink = g.addNode<CaptureSinkNode>(1);
  auto& U = static_cast<Up4Node&>(g.node(up));
  auto& D = static_cast<Down4Node&>(g.node(down));
  g.connect(src, static_cast<SineNode&>(g.node(src)).out(), up, U.in());

  PortId tailOut = kNoPort;
  NodeId tail{};
  if (stage == 0) { // library GainNode for drive, then a memoryless tanh
    const NodeId drive = g.addNode<ConstFloatNode>(kDrive);
    const NodeId gain = g.addNode<GainNode>(kFactor);
    const NodeId shape = g.addNode<TanhNode>(kFactor);
    auto& G = static_cast<GainNode&>(g.node(gain));
    auto& T = static_cast<TanhNode&>(g.node(shape));
    g.connect(drive, static_cast<ConstFloatNode&>(g.node(drive)).value(), gain, G.gain());
    g.connect(up, U.out(), gain, G.in());
    g.connect(gain, G.out(), shape, T.in());
    tail = shape;
    tailOut = T.out();
  } else if (stage == 1) {
    tail = g.addNode<PhaseAwareFeedbackTanhNode>();
    auto& S = static_cast<PhaseAwareFeedbackTanhNode&>(g.node(tail));
    g.connect(up, U.out(), tail, S.in());
    tailOut = S.out();
  } else {
    tail = g.addNode<StockFeedbackTanhNode>(kFactor);
    auto& S = static_cast<StockFeedbackTanhNode&>(g.node(tail));
    g.connect(up, U.out(), tail, S.in());
    tailOut = S.out();
  }
  g.connect(tail, tailOut, down, D.in());
  g.connect(down, D.out(), sink, static_cast<CaptureSinkNode&>(g.node(sink)).in());
  return sink;
}

NodeId naiveChain(Graph& g) {
  const NodeId src = g.addNode<SineNode>();
  const NodeId drive = g.addNode<ConstFloatNode>(kDrive);
  const NodeId gain = g.addNode<GainNode>(1);
  const NodeId shape = g.addNode<TanhNode>(1);
  const NodeId sink = g.addNode<CaptureSinkNode>(1);
  auto& G = static_cast<GainNode&>(g.node(gain));
  auto& T = static_cast<TanhNode&>(g.node(shape));
  g.connect(src, static_cast<SineNode&>(g.node(src)).out(), gain, G.in());
  g.connect(drive, static_cast<ConstFloatNode&>(g.node(drive)).value(), gain, G.gain());
  g.connect(gain, G.out(), shape, T.in());
  g.connect(shape, T.out(), sink, static_cast<CaptureSinkNode&>(g.node(sink)).in());
  return sink;
}

NodeId nestedChain(Graph& g) {
  const NodeId src = g.addNode<SineNode>();
  const NodeId os = g.addNode<Oversampled4xNode>();
  const NodeId sink = g.addNode<CaptureSinkNode>(1);
  auto& O = static_cast<Oversampled4xNode&>(g.node(os));
  g.connect(src, static_cast<SineNode&>(g.node(src)).out(), os, O.in());
  g.connect(os, O.out(), sink, static_cast<CaptureSinkNode&>(g.node(sink)).in());
  return sink;
}

} // namespace

int main() {
  std::printf("4x oversampling probe: %.0f Hz base rate, %u frames, %d-tap filters, "
              "%.0f Hz tone\n\n", kRate, kTotal, kTaps, kFreq);

  // -------------------------------------------------------------------------
  std::printf("--- Part 1: rate as buffer length (the literal reading) ---\n");
  for (std::uint32_t block : {64u, 480u}) {
    Graph g;
    const NodeId src = g.addNode<SineNode>();
    const NodeId up = g.addNode<RateAsLengthUpNode>();
    const NodeId probe = g.addNode<FrameProbeNode>();
    auto& U = static_cast<RateAsLengthUpNode&>(g.node(up));
    auto& P = static_cast<FrameProbeNode&>(g.node(probe));
    g.connect(src, static_cast<SineNode&>(g.node(src)).out(), up, U.in());
    g.connect(up, U.out(), probe, P.in());
    Engine e(g, kRate, kMaxBlock);
    e.process(block);
    std::printf("  block %3u: upsampler needs %4u output frames, its view offers %3u; "
                "the next node is told numFrames=%u, its input view has %u\n",
                block, U.wanted, U.offered, P.told, P.viewed);
  }
  std::printf("  => fails: every port's view is the block's numFrames, so a 4x signal "
              "has nowhere to go.\n\n");

  // -------------------------------------------------------------------------
  std::printf("--- Part 2: polyphase, memoryless tanh ---\n");
  std::printf("  Sine -> Up4 [audio<1>->audio<4>] -> GainNode(4) -> Tanh(4) "
              "-> Down4 [audio<4>->audio<1>] -> CaptureSink\n");
  const auto ref = reference4x(kDrive, Tanh{});
  printDiffs("vs plain 4x reference:", ref, [](Graph& g) { return polyphaseChain(g, 0); });
  const auto naive = render(naiveChain, constantBlocks(64));
  const auto poly = render([](Graph& g) { return polyphaseChain(g, 0); }, constantBlocks(64));
  std::printf("  aliasing (non-harmonic / harmonic energy):\n");
  std::printf("    1x, no oversampling   %7.1f dB\n", aliasDb(naive));
  std::printf("    4x polyphase graph    %7.1f dB\n", aliasDb(poly));
  std::printf("    4x plain reference    %7.1f dB\n\n", aliasDb(ref));

  // -------------------------------------------------------------------------
  std::printf("--- Part 3: polyphase, stateful tanh (y = tanh(%.0f x + %.1f y)) ---\n",
              kFbDrive, kFb);
  const auto refFb = reference4x(1.0f, FeedbackTanh{});
  printDiffs("phase-aware node (one state, walks phases in time order):", refFb,
             [](Graph& g) { return polyphaseChain(g, 1); });
  const auto stock = [](Graph& g) { return polyphaseChain(g, 2); };
  printDiffs("stock node (one state per channel, the way library nodes are written):",
             refFb, stock);
  {
    // Wrong, but is it *detectably* wrong? The repo's invariance tests compare
    // renders across block sizes, so ask what they would ask.
    const auto first = render(stock, constantBlocks(1));
    bool same = true;
    for (const Plans& p : allPlans()) same = same && render(stock, p.plan) == first;
    std::printf("  stock node's renders identical across all block sizes: %s\n",
                same ? "yes" : "no");
  }
  {
    Graph g;
    const NodeId src = g.addNode<SineNode>();
    const NodeId up = g.addNode<Up4Node>();
    const NodeId shape = g.addNode<PhaseAwareFeedbackTanhNode>();
    const NodeId quad = g.addNode<CaptureSinkNode>(4); // stands in for a 4-speaker output
    auto& U = static_cast<Up4Node&>(g.node(up));
    auto& S = static_cast<PhaseAwareFeedbackTanhNode&>(g.node(shape));
    g.connect(src, static_cast<SineNode&>(g.node(src)).out(), up, U.in());
    g.connect(up, U.out(), shape, S.in());
    g.connect(shape, S.out(), quad, static_cast<CaptureSinkNode&>(g.node(quad)).in());
    Engine e(g, kRate, kMaxBlock);
    std::printf("  connect(polyphase audio<4> -> 4-channel speaker sink): accepted, "
                "PortType::audio(4) == PortType::audio(4)\n");
    std::printf("  sample rate the at-4x node saw in prepare(): %.0f\n\n", S.preparedRate);
  }

  // -------------------------------------------------------------------------
  std::printf("--- Part 4: a node owning a 4x-rate Engine, stock stateful node inside ---\n");
  printDiffs("vs the same plain 4x reference:", refFb, nestedChain);
  {
    Graph g;
    nestedChain(g);
    Engine e(g, kRate, kMaxBlock);
    for (NodeId id : g.nodeIds())
      if (auto* o = dynamic_cast<Oversampled4xNode*>(&g.node(id)))
        std::printf("  sample rate the inner stock node saw in prepare(): %.0f\n",
                    o->shaperNode().preparedRate);
    std::printf("  outer graph's node count: %zu (the inner graph is not part of it)\n",
                g.nodeCount());
  }
  return 0;
}
