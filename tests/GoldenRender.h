#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Golden renders: a test pins what a clip renders to, as its frame count and a
// hash of each block of 4,096 frames, committed in the test. A change that alters
// the rendered bits updates the values in the same commit, so review sees every
// such change. The same values hold in every build, native or WebAssembly, which
// is how a browser's render is checked against a native one (map ticket 08).
//
// The hash is FNV-1a, 64-bit, over each sample's IEEE 754 bits, their four bytes
// least significant first, frames in order and left before right, started afresh
// for every block. JavaScript computes the same with BigInt.
namespace golden {

inline constexpr std::size_t kBlockFrames = 4096;

// What a clip renders to at one sample rate.
struct Render {
  double rate = 0;
  std::uint64_t frames = 0;
  std::vector<std::uint64_t> blocks; // one hash per 4,096 frames, the last for the rest
};

// The hash of frames [from, to) of a stereo render.
inline std::uint64_t hash(const std::vector<float>& left, const std::vector<float>& right,
                          std::size_t from, std::size_t to) {
  std::uint64_t h = 14695981039346656037ull;
  const auto mix = [&h](float sample) {
    std::uint32_t bits;
    std::memcpy(&bits, &sample, sizeof bits);
    for (int i = 0; i < 4; ++i) {
      h ^= (bits >> (8 * i)) & 0xffu;
      h *= 1099511628211ull;
    }
  };
  for (std::size_t i = from; i < to; ++i) {
    mix(left[i]);
    mix(right[i]);
  }
  return h;
}

// A stereo render's golden values. The channels have the same length.
inline Render measure(double rate, const std::vector<float>& left,
                      const std::vector<float>& right) {
  Render render{rate, left.size(), {}};
  for (std::size_t from = 0; from < left.size(); from += kBlockFrames)
    render.blocks.push_back(
        hash(left, right, from, std::min(from + kBlockFrames, left.size())));
  return render;
}

// "" when the render matches its golden values; otherwise its frame count
// against the golden one, and its first differing block.
inline std::string difference(const Render& expected, const Render& actual) {
  const auto common = std::min(expected.blocks.size(), actual.blocks.size());
  std::size_t block = 0;
  while (block < common && actual.blocks[block] == expected.blocks[block]) ++block;
  if (actual.frames == expected.frames && block == actual.blocks.size()) return "";
  char text[160];
  std::snprintf(text, sizeof text,
                "%.0f Hz: %llu frames (golden %llu), first differing block %zu "
                "(frames %zu on)",
                actual.rate, static_cast<unsigned long long>(actual.frames),
                static_cast<unsigned long long>(expected.frames), block,
                block * kBlockFrames);
  return text;
}

// The renders' values as C++ initializers, to paste over the golden ones.
inline std::string source(const std::vector<Render>& renders) {
  std::string text;
  char line[64];
  for (const auto& render : renders) {
    std::snprintf(line, sizeof line, "    {%.0f, %llu, {", render.rate,
                  static_cast<unsigned long long>(render.frames));
    text += line;
    for (std::size_t b = 0; b < render.blocks.size(); ++b) {
      std::snprintf(line, sizeof line, "%s0x%016llxull", b % 3 == 0 ? "\n        " : " ",
                    static_cast<unsigned long long>(render.blocks[b]));
      text += line;
      if (b + 1 < render.blocks.size()) text += ",";
    }
    text += "}},\n";
  }
  return text;
}

} // namespace golden
