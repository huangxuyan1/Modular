#include "modular/nodes/AudioOutputNode.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmeapi.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace modular::nodes {
namespace {
constexpr std::uint32_t kTimeout = 0x10000;
constexpr std::uint32_t kWaitFailed = 0x10001;

void check(MMRESULT result, const char* operation) {
  if (result != MMSYSERR_NOERROR)
    throw std::invalid_argument(std::string("audio output: ") + operation +
                                " failed (WinMM " + std::to_string(result) + ")");
}
} // namespace

struct AudioOutputNode::Impl {
  struct Buffer {
    WAVEHDR header{};
    std::vector<std::int16_t> samples;
    bool prepared = false;
    bool pending = false;
  };

  HWAVEOUT device = nullptr;
  HANDLE event = nullptr;
  std::array<Buffer, 8> buffers;
  std::size_t next = 0;
  std::uint32_t max_frames = 0;
  DWORD timeout_ms = 0;

  ~Impl() {
    if (device) {
      // Reset returns every outstanding header before any backing storage dies.
      waveOutReset(device);
      for (auto& buffer : buffers)
        if (buffer.prepared)
          waveOutUnprepareHeader(device, &buffer.header, sizeof(WAVEHDR));
      waveOutClose(device);
    }
    if (event) CloseHandle(event);
  }

  std::uint32_t wait(Buffer& buffer) {
    if (!buffer.pending) return 0;
    const auto deadline = GetTickCount64() + timeout_ms;
    while ((buffer.header.dwFlags & WHDR_DONE) == 0) {
      const auto now = GetTickCount64();
      if (now >= deadline) return kTimeout;
      const auto result = WaitForSingleObject(event, static_cast<DWORD>(deadline - now));
      if (result == WAIT_TIMEOUT) return kTimeout;
      if (result != WAIT_OBJECT_0) return kWaitFailed;
    }
    buffer.pending = false;
    return 0;
  }
};

AudioOutputNode::AudioOutputNode(Graph& graph, std::uint32_t channels)
    : Node(graph), channels_(channels) {
  if (channels != 1 && channels != 2)
    throw std::invalid_argument("audio output: requires mono or stereo");
  in_ = addInput(PortType::audio(channels), "in");
}

AudioOutputNode::~AudioOutputNode() = default;

void AudioOutputNode::release() noexcept { impl_.reset(); }

void AudioOutputNode::setChannels(Graph& graph, NodeId self, std::uint32_t channels) {
  if (channels != 1 && channels != 2)
    throw std::invalid_argument("audio output: requires mono or stereo");
  const std::array changes{PortTypeChange{in_, PortType::audio(channels)}};
  setPortTypes(graph, self, changes);
  channels_ = channels;
}

void AudioOutputNode::prepare(const PrepareInfo& info) {
  if (!std::isfinite(info.sampleRate) || info.sampleRate < 1000 ||
      info.sampleRate > 384000 || std::floor(info.sampleRate) != info.sampleRate)
    throw std::invalid_argument("audio output: requires an integer rate from 1000 to 384000");
  if (info.maxBlockFrames == 0 || info.maxBlockFrames > info.sampleRate)
    throw std::invalid_argument("audio output: block size must be between one frame and one second");

  impl_.reset();
  error_.store(0);
  auto state = std::make_unique<Impl>();
  state->max_frames = info.maxBlockFrames;
  state->timeout_ms = static_cast<DWORD>(
      std::ceil(1000 * state->buffers.size() * info.maxBlockFrames / info.sampleRate)) + 2000;
  state->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!state->event) throw std::invalid_argument("audio output: cannot create device event");

  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = static_cast<WORD>(channels_);
  format.nSamplesPerSec = static_cast<DWORD>(info.sampleRate);
  format.wBitsPerSample = 16;
  format.nBlockAlign = static_cast<WORD>(channels_ * sizeof(std::int16_t));
  format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
  check(waveOutOpen(&state->device, WAVE_MAPPER, &format,
                    reinterpret_cast<DWORD_PTR>(state->event), 0, CALLBACK_EVENT), "open");
  for (auto& buffer : state->buffers) {
    buffer.samples.resize(static_cast<std::size_t>(info.maxBlockFrames) * channels_);
    buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.samples.data());
    buffer.header.dwBufferLength = static_cast<DWORD>(
        buffer.samples.size() * sizeof(std::int16_t));
    check(waveOutPrepareHeader(state->device, &buffer.header, sizeof(WAVEHDR)), "prepare");
    buffer.prepared = true;
  }
  impl_ = std::move(state);
}

void AudioOutputNode::process(ProcessContext& ctx) {
  assert(impl_ && ctx.numFrames() <= impl_->max_frames);
  if (failed() || ctx.numFrames() == 0) return;
  auto& buffer = impl_->buffers[impl_->next];
  if (const auto error = impl_->wait(buffer); error != 0) {
    error_.store(error);
    return;
  }
  const auto input = ctx.audioIn(in_);
  for (std::uint32_t frame = 0; frame < ctx.numFrames(); ++frame) {
    for (std::uint32_t channel = 0; channel < channels_; ++channel) {
      const float sample = input.channels[channel][frame];
      const float clamped = std::isfinite(sample) ? std::clamp(sample, -1.0f, 1.0f) : 0;
      buffer.samples[static_cast<std::size_t>(frame) * channels_ + channel] =
          static_cast<std::int16_t>(clamped * 32767.0f);
    }
  }
  buffer.header.dwBufferLength = ctx.numFrames() * channels_ * sizeof(std::int16_t);
  const auto error = waveOutWrite(impl_->device, &buffer.header, sizeof(WAVEHDR));
  if (error != MMSYSERR_NOERROR) {
    error_.store(error);
    return;
  }
  buffer.pending = true;
  impl_->next = (impl_->next + 1) % impl_->buffers.size();
}

bool AudioOutputNode::drain() {
  if (failed()) return false;
  if (!impl_) return true;
  for (auto& buffer : impl_->buffers) {
    if (const auto error = impl_->wait(buffer); error != 0) {
      error_.store(error);
      return false;
    }
  }
  return true;
}

std::string AudioOutputNode::errorMessage() const {
  const auto error = error_.load();
  if (error == 0) return {};
  if (error == kTimeout) return "audio output: device did not return a buffer in time";
  if (error == kWaitFailed) return "audio output: waiting for the device failed";
  return "audio output: write failed (WinMM " + std::to_string(error) + ")";
}

} // namespace modular::nodes
