// What the web module was built from: the commit of the tree, whether that tree had
// uncommitted changes, and the Emscripten version. apps/web/stamp_version.cmake writes
// WebVersion.h each time the module is built, and the loader, modular.mjs, reads these
// once as loadModular()'s `version`. Built only under Emscripten.
#include <cstdint>

#include <emscripten/emscripten.h>

#include "WebVersion.h"

extern "C" {

// The commit, as 40 hexadecimal digits, and the Emscripten version, as UTF-8.
EMSCRIPTEN_KEEPALIVE const char* modular_version_commit() {
  return modular::web::version::kCommit;
}
EMSCRIPTEN_KEEPALIVE const char* modular_version_emscripten() {
  return modular::web::version::kEmscripten;
}

// 1 when the tree had uncommitted changes to tracked files, else 0.
EMSCRIPTEN_KEEPALIVE std::int32_t modular_version_dirty() {
  return modular::web::version::kDirty ? 1 : 0;
}

} // extern "C"
