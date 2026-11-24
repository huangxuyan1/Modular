// The render core's golden clips (seam S1), part of web_core_tests. Each clip
// renders through a fresh instrument at three rates and must match the values
// committed in WebGoldenClips.h, frame count and per-block hashes, in every build:
// natively in Debug and Release, and under Node in the web build, so the module's
// render equals the native one by transitivity.
#include "doctest.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "GoldenRender.h"
#include "Instrument.h"
#include "WebGoldenClips.h"

using modular::web::Instrument;

namespace {

// Renders `midi` through a fresh instrument at each golden render's rate. On a
// mismatch it fails with each differing rate's frame count and first differing
// block, then every rate's values, to paste over the golden ones.
void checkGolden(const webgolden::Bytes& midi, std::uint32_t voiceLimit,
                 const std::vector<golden::Render>& goldens) {
  const auto font = webgolden::soundFont();
  std::vector<golden::Render> renders;
  std::string differences;
  for (const auto& expected : goldens) {
    const auto rendering = Instrument(font, voiceLimit).render(midi, expected.rate);
    renders.push_back(golden::measure(expected.rate, rendering.left, rendering.right));
    const auto difference = golden::difference(expected, renders.back());
    if (!difference.empty()) differences += difference + "\n";
  }
  if (!differences.empty())
    FAIL_CHECK(differences << "the render's values:\n" << golden::source(renders));
}

} // namespace

TEST_CASE("notes on a frame's half, then a tempo change, render their golden bits at three rates") {
  checkGolden(webgolden::timingClip(), modular::nodes::SoundFontNode::kDefaultVoiceLimit,
              webgolden::kTimingGolden);
}

TEST_CASE("a dense passage through a pool of four voices renders its golden bits at three rates") {
  const auto clip = webgolden::poolClip();
  // Notes were taken: with room for every note, the render differs.
  const auto font = webgolden::soundFont();
  CHECK(Instrument(font, webgolden::kPoolVoiceLimit).render(clip, 48000).left !=
        Instrument(font).render(clip, 48000).left);
  checkGolden(clip, webgolden::kPoolVoiceLimit, webgolden::kPoolGolden);
}

TEST_CASE("a one-note preview renders its golden bits at three rates") {
  checkGolden(webgolden::previewClip(), modular::nodes::SoundFontNode::kDefaultVoiceLimit,
              webgolden::kPreviewGolden);
}
