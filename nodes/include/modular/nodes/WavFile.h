#pragma once

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// Minimal 16-bit PCM WAV writer for the offline render sink. Not a general
// audio-file library and not intended to become one.

namespace modular::nodes {

inline void writeWav16(const std::filesystem::path& path, const std::vector<float>& interleaved,
                       std::uint32_t channels, std::uint32_t sampleRate) {
  // RIFF sizes are 32-bit. Refuse an unrepresentable take before creating a
  // file, so a recorder can retain it instead of publishing a corrupt header.
  if (channels == 0 || channels > 32767 || sampleRate == 0 ||
      interleaved.size() % channels != 0 ||
      interleaved.size() > (std::numeric_limits<std::uint32_t>::max() - 36ULL) / 2 ||
      std::uint64_t(sampleRate) * channels * 2 > std::numeric_limits<std::uint32_t>::max())
    throw std::invalid_argument("wav: samples or format exceed PCM RIFF limits");
  std::ofstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("writeWav16: cannot open output file");

  const std::uint32_t dataBytes =
      static_cast<std::uint32_t>(interleaved.size()) * 2;
  const std::uint32_t byteRate = sampleRate * channels * 2;
  const std::uint16_t blockAlign = static_cast<std::uint16_t>(channels * 2);

  auto u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
  auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };

  f.write("RIFF", 4);
  u32(36 + dataBytes);
  f.write("WAVE", 4);
  f.write("fmt ", 4);
  u32(16);
  u16(1); // PCM
  u16(static_cast<std::uint16_t>(channels));
  u32(sampleRate);
  u32(byteRate);
  u16(blockAlign);
  u16(16); // bits per sample
  f.write("data", 4);
  u32(dataBytes);

  for (float s : interleaved) {
    const float clamped = std::isnan(s) ? 0.0f : std::clamp(s, -1.0f, 1.0f);
    const auto v = static_cast<std::int16_t>(clamped * 32767.0f);
    f.write(reinterpret_cast<const char*>(&v), 2);
  }
  f.close();
  if (!f) throw std::runtime_error("writeWav16: failed to write output file");
}

} // namespace modular::nodes
