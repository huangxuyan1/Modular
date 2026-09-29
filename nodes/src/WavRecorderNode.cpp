#include "modular/nodes/WavRecorderNode.h"

#include <cmath>
#include <random>
#include <array>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "modular/nodes/Payloads.h"
#include "modular/nodes/WavFile.h"

namespace modular::nodes {
namespace {
std::string writeErrorText(const std::error_code& error) {
  // System exception text uses the Windows ANSI code page, whereas node status
  // is UTF-8. Report stable, actionable text from the code instead of what().
  if (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory)
    return "Output folder not found.";
  if (error == std::errc::permission_denied)
    return "Cannot write to the output folder. Check its permissions.";
  if (error == std::errc::no_space_on_device)
    return "The output drive is full.";
  if (error == std::errc::invalid_argument || error == std::errc::filename_too_long)
    return "The output path is invalid or too long.";
  return "Could not save WAV (system error " + std::to_string(error.value()) + ").";
}

std::filesystem::path normalizeDirectory(std::filesystem::path directory) {
#ifdef _WIN32
  // Explorer's Copy as path includes quotes. They cannot be filename characters
  // on Windows; accept that pasted form in both authored and restored settings.
  const auto& text = directory.native();
  if (text.size() >= 2 && text.front() == L'"' && text.back() == L'"')
    directory = text.substr(1, text.size() - 2);
  if (directory.native().find(L'"') != std::wstring::npos)
    throw std::invalid_argument("recorder: output directory contains an unmatched quote");
#endif
  return directory;
}

void validateSettings(const std::filesystem::path& directory,
                       const std::string& prefix, std::uint32_t channels) {
  if (channels == 0 || channels > 64)
    throw std::invalid_argument("recorder: channels must be from 1 to 64");
  if (directory.empty() || prefix.empty() ||
      prefix.find_first_of("/\\:*?\"<>|") != std::string::npos ||
      std::any_of(prefix.begin(), prefix.end(), [](unsigned char c) { return c < 32; }) ||
      directory.native().find(std::filesystem::path::value_type{}) !=
          std::filesystem::path::string_type::npos)
    throw std::invalid_argument("recorder: enter an output directory and filename prefix");
}
// Atomically reserve an owned directory. Never clean a guessed filename or a
// directory another recorder created, even when several nodes share a prefix.
class PendingTake {
public:
  explicit PendingTake(const std::filesystem::path& output) {
    std::random_device random;
    for (int attempt = 0; attempt < 16; ++attempt) {
      const auto candidate = output / (".modular-take-" + std::to_string(random()));
      if (std::filesystem::create_directory(candidate)) {
        directory_ = candidate;
        file_ = directory_ / "take.tmp";
        return;
      }
    }
    throw std::runtime_error("recorder: cannot reserve temporary output");
  }
  ~PendingTake() {
    std::error_code error;
    std::filesystem::remove(file_, error);
    std::filesystem::remove(directory_, error);
  }
  const std::filesystem::path& file() const { return file_; }
private:
  std::filesystem::path directory_, file_;
};
}

WavRecorderNode::WavRecorderNode(Graph& graph, std::filesystem::path directory,
                                 std::string prefix, std::uint32_t channels, Writer writer)
    : Node(graph), directory_(normalizeDirectory(std::move(directory))),
      prefix_(std::move(prefix)),
      channels_(channels), writer_(std::move(writer)) {
  if (!writer_) throw std::invalid_argument("recorder: missing WAV writer");
  validateSettings(directory_, prefix_, channels);
  in_ = addInput(PortType::audio(channels), "in");
  start_ = addInput(PortType::event(subtype::Trigger), "start");
  stop_ = addInput(PortType::event(subtype::Trigger), "stop");
}

void WavRecorderNode::configure(Graph& graph, NodeId self,
                                 std::filesystem::path directory, std::string prefix,
                                 std::uint32_t channels) {
  directory = normalizeDirectory(std::move(directory));
  validateSettings(directory, prefix, channels);
  const std::array changes{PortTypeChange{in_, PortType::audio(channels)}};
  setPortTypes(graph, self, changes);
  directory_.swap(directory);
  prefix_.swap(prefix);
  channels_ = channels;
}

void WavRecorderNode::prepare(const PrepareInfo& info) {
  if (!std::isfinite(info.sampleRate) || info.sampleRate < 1000 ||
      info.sampleRate > 384000 || std::floor(info.sampleRate) != info.sampleRate)
    throw std::invalid_argument("recorder: requires an integer rate from 1000 to 384000");
  release();
  sample_rate_ = static_cast<std::uint32_t>(info.sampleRate);
}

void WavRecorderNode::release() noexcept {
  request_.store(Request::None);
  std::vector<float>().swap(samples_);
  publish(State::Idle, "Idle");
}

std::string WavRecorderNode::statusText() const {
  std::lock_guard lock(status_mutex_);
  return status_;
}

void WavRecorderNode::publish(State state, std::string text) {
  std::lock_guard lock(status_mutex_);
  status_ = std::move(text);
  state_.store(state);
}

void WavRecorderNode::retry() noexcept {
  if (state() == State::Failed) request_.store(Request::Retry);
}

void WavRecorderNode::discard() noexcept {
  if (state() == State::Failed) request_.store(Request::Discard);
}

void WavRecorderNode::completeTake() {
  publish(State::Writing, "Writing WAV...");
  try {
    writeTake();
  } catch (const std::system_error& error) {
    publish(State::Failed, writeErrorText(error.code()) + " Take retained; Retry or Discard.");
  } catch (const std::invalid_argument&) {
    publish(State::Failed,
        "Take exceeds WAV format limits. Take retained; Retry or Discard.");
  } catch (const std::exception&) {
    publish(State::Failed, "Could not write WAV. Take retained; Retry or Discard.");
  }
}

void WavRecorderNode::writeTake() {
  PendingTake pending(directory_);
  writer_(pending.file(), samples_, channels_, sample_rate_);
  for (std::uint64_t number = 1; number != 0; ++number) {
    const auto destination = directory_ / std::filesystem::u8path(
        prefix_ + "-" + std::to_string(number) + ".wav");
    std::error_code error;
    // Publish without replacing any existing path, including a competing take.
    // Windows rename also works on removable filesystems without hard links.
#ifdef _WIN32
    if (!MoveFileExW(pending.file().c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
      error = std::error_code(GetLastError(), std::system_category());
#else
    std::filesystem::create_hard_link(pending.file(), destination, error);
#endif
    if (!error) {
      std::vector<float>().swap(samples_);
      const auto utf8 = destination.u8string();
      publish(State::Idle, "Saved " + std::string(utf8.begin(), utf8.end()));
      return;
    }
    if (std::filesystem::symlink_status(destination).type() !=
        std::filesystem::file_type::not_found) continue;
    throw std::system_error(error);
  }
  throw std::runtime_error("recorder: take numbers exhausted");
}

void WavRecorderNode::process(ProcessContext& ctx) {
  const auto request = request_.exchange(Request::None);
  if (state() == State::Failed) {
    if (request == Request::Retry) completeTake();
    else if (request == Request::Discard) {
      std::vector<float>().swap(samples_);
      publish(State::Idle, "Take discarded");
    }
  }
  auto start = ctx.eventsIn(start_).begin();
  const auto startsEnd = ctx.eventsIn(start_).end();
  auto stop = ctx.eventsIn(stop_).begin();
  const auto stopsEnd = ctx.eventsIn(stop_).end();
  const auto audio = ctx.audioIn(in_);
  for (std::uint32_t frame = 0; frame < ctx.numFrames(); ++frame) {
    const bool stopping = stop != stopsEnd && stop->offset == frame;
    const bool starting = start != startsEnd && start->offset == frame;
    while (start != startsEnd && start->offset == frame) ++start;
    while (stop != stopsEnd && stop->offset == frame) ++stop;
    if (stopping && state() == State::Recording) completeTake();
    else if (starting && !stopping && state() == State::Idle)
      publish(State::Recording, "Recording");
    if (state() == State::Recording)
      for (std::uint32_t channel = 0; channel < channels_; ++channel)
        samples_.push_back(audio.channels[channel][frame]);
  }
}

} // namespace modular::nodes
