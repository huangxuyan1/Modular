#include "modular/GraphPersistence.h"

#include <fstream>
#include <random>
#include <sstream>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace modular {
namespace {

// Reserve a sibling directory atomically, so cleanup can only touch this save's
// own file. Sharing the destination filesystem also permits atomic replacement.
class PendingSave {
public:
  explicit PendingSave(const std::filesystem::path& destination) {
    std::random_device random;
    for (int attempt = 0; attempt < 16; ++attempt) {
      auto candidate = destination;
      candidate += ".save-" + std::to_string(random()) + "-" + std::to_string(random());
      if (std::filesystem::create_directory(candidate)) {
        directory_ = std::move(candidate);
        file_ = directory_ / "graph.tmp";
        return;
      }
    }
    throw std::invalid_argument("save: could not reserve temporary file");
  }
  ~PendingSave() {
    std::error_code error;
    std::filesystem::remove(file_, error);
    std::filesystem::remove(directory_, error);
  }
  const std::filesystem::path& file() const { return file_; }
private:
  std::filesystem::path directory_;
  std::filesystem::path file_;
};

} // namespace

void saveGraphFile(const Workspace& workspace, const std::filesystem::path& path,
                    std::span<const NodeCodec> codecs) {
  std::ostringstream encoded;
  saveGraph(workspace, encoded, codecs);
  try {
    if (path.empty()) throw std::invalid_argument("save: empty graph path");
    PendingSave pending(path);
    std::ofstream output(pending.file(), std::ios::binary);
    const auto bytes = encoded.str();
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) throw std::invalid_argument("save: could not write complete graph");
#ifdef _WIN32
    if (!MoveFileExW(pending.file().c_str(), path.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      throw std::invalid_argument("save: could not replace destination graph");
#else
    std::filesystem::rename(pending.file(), path);
#endif
  } catch (const std::filesystem::filesystem_error&) {
    throw std::invalid_argument("save: destination is unavailable or not writable");
  }
}

Workspace openGraphFile(const std::filesystem::path& path,
                         std::span<const NodeCodec> codecs) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::invalid_argument("open: could not read graph file");
  return openGraph(input, codecs);
}

} // namespace modular
