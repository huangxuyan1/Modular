// The web module's exported C functions: the render core behind a boundary that
// no C++ exception crosses. Built only under Emscripten. The loader, modular.mjs,
// is their one caller; docs/web.md states the surface a client sees.
//
// Every fallible export catches whatever the render core throws and returns 0,
// leaving the message for modular_error(). So the module links neither
// getExceptionMessage nor the C++ demangler, and an exception that does escape
// can only be a WebAssembly trap or an abort.
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <emscripten/emscripten.h>

#include "Instrument.h"

using modular::web::Instrument;
using modular::web::Rendering;

namespace {

constexpr const char* kOutOfMemory = "out of memory";

// Every instrument made, for the module's life. A handle is an index plus one.
std::vector<std::unique_ptr<Instrument>> instruments;
// The one rendering the module holds, until the loader has copied it out.
Rendering rendering;
std::string errorStorage;
const char* errorText = "";

// Records a failure's message. Copying it may itself run out of memory, which
// then becomes the message instead.
void fail(const char* message) noexcept {
  try {
    errorStorage = message;
    errorText = errorStorage.c_str();
  } catch (...) {
    errorText = kOutOfMemory;
  }
}

// Runs `work`; true when it returns, false with the message recorded when it
// throws anything at all.
template <class Work> bool guarded(Work&& work) noexcept {
  try {
    work();
    return true;
  } catch (const std::bad_alloc&) {
    fail(kOutOfMemory);
  } catch (const std::exception& error) {
    fail(error.what());
  } catch (...) {
    fail("unknown error");
  }
  return false;
}

} // namespace

extern "C" {

// Parses a soundfont's bytes into a new instrument. Returns its handle, never
// 0, or 0 when it fails.
EMSCRIPTEN_KEEPALIVE std::int32_t modular_create_instrument(const std::uint8_t* bytes,
                                                            std::size_t size) {
  std::int32_t handle = 0;
  guarded([&] {
    auto instrument = std::make_unique<Instrument>(std::span{bytes, size});
    instruments.push_back(std::move(instrument));
    handle = static_cast<std::int32_t>(instruments.size());
  });
  return handle;
}

// Renders MIDI file bytes at a sample rate into the module's one rendering,
// releasing the last one first. Returns 1, or 0 when it fails, which leaves no
// rendering.
EMSCRIPTEN_KEEPALIVE std::int32_t modular_render(std::int32_t handle,
                                                 const std::uint8_t* bytes, std::size_t size,
                                                 double sampleRate) {
  rendering = {};
  return guarded([&] {
    if (handle < 1 || static_cast<std::size_t>(handle) > instruments.size())
      throw std::invalid_argument("render: unknown instrument");
    rendering = instruments[static_cast<std::size_t>(handle) - 1]->render(
        std::span{bytes, size}, sampleRate);
  });
}

// The rendering's frames per channel, and where each channel's floats begin.
// A frame count fits 32 bits: wasm32's size_t is 32 bits wide.
EMSCRIPTEN_KEEPALIVE std::uint32_t modular_rendering_frames() {
  return static_cast<std::uint32_t>(rendering.left.size());
}
EMSCRIPTEN_KEEPALIVE const float* modular_rendering_left() {
  return rendering.left.data();
}
EMSCRIPTEN_KEEPALIVE const float* modular_rendering_right() {
  return rendering.right.data();
}

// Frees the rendering's samples once the loader has copied them out.
EMSCRIPTEN_KEEPALIVE void modular_release_rendering() { rendering = {}; }

// The last failure's message, as UTF-8.
EMSCRIPTEN_KEEPALIVE const char* modular_error() { return errorText; }

} // extern "C"
