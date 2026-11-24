// Linked only into the test module, modular_web_test: a variant of the web module built for
// the smoke test and never shipped. Beside the module's own exports it gives the test what
// the shipped module cannot:
//
// - a WebAssembly trap on demand: any allocation of 64 MiB or more traps, as a fault deep
//   inside a render would, instead of throwing std::bad_alloc;
// - the heap in use, which shows that a rendering is released once copied out.
//
// tests/web_test_module.js, linked with it, hands each instance to the test.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <malloc.h>
#include <new>

#include <emscripten/emscripten.h>

namespace {
constexpr std::size_t kTrapBytes = std::size_t{64} << 20;
} // namespace

void* operator new(std::size_t size) {
  if (size >= kTrapBytes) __builtin_trap();
  if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
  throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

// The bytes the module's allocator has handed out and not yet had back.
extern "C" EMSCRIPTEN_KEEPALIVE std::uint32_t modular_test_heap_bytes() {
  return static_cast<std::uint32_t>(mallinfo().uordblks);
}
