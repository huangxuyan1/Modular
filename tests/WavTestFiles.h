#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

struct WavTestFiles {
  std::filesystem::path directory = std::filesystem::temp_directory_path() /
      ("modular-recorder-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  WavTestFiles() {
    if (!std::filesystem::create_directory(directory))
      throw std::runtime_error("could not reserve test directory");
  }
  ~WavTestFiles() {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
  }
};

struct TestWav {
  std::uint32_t channels;
  std::uint32_t sampleRate;
  std::vector<std::int16_t> samples;
};

inline TestWav readTestWav(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), {}};
  if (bytes.size() < 44 || std::string(bytes.begin(), bytes.begin() + 4) != "RIFF" ||
      std::string(bytes.begin() + 8, bytes.begin() + 12) != "WAVE")
    throw std::runtime_error("missing or invalid WAV artifact");
  const auto number = [&](std::size_t offset, int size) {
    std::uint32_t value = 0;
    for (int i = 0; i < size; ++i) value |= std::uint32_t(bytes.at(offset + i)) << (8 * i);
    return value;
  };
  if (number(4, 4) != bytes.size() - 8 || number(40, 4) != bytes.size() - 44 ||
      number(20, 2) != 1 || number(34, 2) != 16)
    throw std::runtime_error("incomplete or unsupported WAV artifact");
  TestWav wav{number(22, 2), number(24, 4), {}};
  for (std::size_t i = 44; i < bytes.size(); i += 2)
    wav.samples.push_back(static_cast<std::int16_t>(number(i, 2)));
  return wav;
}
