#pragma once

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "modular/Engine.h"

namespace modular {

// Single-client control of a worker borrowing a graph. Only Idle/Failed permit
// authored edits: they are published after Engine destruction and node release.
// Layout and node-declared thread-safe runtime inputs may change while active.
class GraphExecution {
public:
  enum class State { Idle, Starting, Running, Stopping, Failed };
  ~GraphExecution() { stop(); if (worker_.joinable()) worker_.join(); }

  State state() const { return state_.load(); }
  bool active() const {
    const auto current = state();
    return current != State::Idle && current != State::Failed;
  }
  std::string errorMessage() const {
    std::lock_guard lock(error_mutex_);
    return error_;
  }

  void start(Graph& graph, double sampleRate = 48000, std::uint32_t block = 512) {
    if (active()) throw std::invalid_argument("execution: already active");
    if (!std::isfinite(sampleRate) || sampleRate <= 0 || block == 0)
      throw std::invalid_argument("execution: invalid rate or block size");
    if (worker_.joinable()) worker_.join();
    { std::lock_guard lock(error_mutex_); error_.clear(); }
    state_.store(State::Starting);
    try {
      worker_ = std::jthread([this, &graph, sampleRate, block](std::stop_token stop) {
        try {
          {
            Engine engine(graph, sampleRate, block);
            auto expected = State::Starting;
            state_.compare_exchange_strong(expected, State::Running);
            const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(block / sampleRate));
            auto next = std::chrono::steady_clock::now();
            std::mutex waitMutex;
            std::condition_variable_any wake;
            while (!stop.stop_requested()) {
              engine.process(block);
              if (const auto error = engine.errorMessage(); !error.empty())
                throw std::runtime_error(error);
              next = std::max(next + period, std::chrono::steady_clock::now());
              std::unique_lock lock(waitMutex);
              wake.wait_until(lock, stop, next, [] { return false; });
            }
          }
          state_.store(State::Idle);
        } catch (const std::exception& error) {
          { std::lock_guard lock(error_mutex_); error_ = error.what(); }
          state_.store(State::Failed);
        }
      });
    } catch (...) {
      state_.store(State::Idle);
      throw;
    }
  }

  // Nonblocking: the client remains responsive while a bounded node wait ends.
  void stop() {
    if (!active()) return;
    worker_.request_stop();
    auto current = state();
    while ((current == State::Starting || current == State::Running) &&
           !state_.compare_exchange_weak(current, State::Stopping)) {}
  }

private:
  std::atomic<State> state_{State::Idle};
  mutable std::mutex error_mutex_;
  std::string error_;
  std::jthread worker_;
};

} // namespace modular
