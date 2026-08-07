#include "pluviora.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

std::vector<uint8_t> readFile(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("cannot open " + path.string());
  }
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

bool startsWith(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         value.substr(0, prefix.size()) == prefix;
}

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "usage: pluviora_archive_test json_dir js_dir font.ttf\n";
    return 2;
  }

  const fs::path json_dir(argv[1]);
  const fs::path js_dir(argv[2]);
  const auto font = readFile(argv[3]);
  std::vector<fs::path> documents;
  for (const fs::directory_entry& entry : fs::directory_iterator(json_dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      documents.push_back(entry.path());
    }
  }
  std::sort(documents.begin(), documents.end());

  PluvioraHandle engine = pluviora_create();
  if (!engine ||
      pluviora_set_font_data(engine, font.data(), font.size()) != PLUVIORA_OK) {
    std::cerr << "could not initialize native engine\n";
    return 1;
  }

  size_t loaded = 0;
  size_t strict_order = 0;
  size_t traversal_order = 0;
  size_t failed = 0;
  size_t charts_with_skipped_animations = 0;
  for (const fs::path& document : documents) {
    const auto json = readFile(document);
    fs::path script = js_dir / document.filename();
    script.replace_extension(".js");
    const auto js = fs::exists(script) ? readFile(script) : std::vector<uint8_t>{};
    const PluvioraStatus status = pluviora_load(
        engine, json.data(), json.size(), js.data(), js.size());
    if (status != PLUVIORA_OK) {
      ++failed;
      std::cerr << document.filename().string() << ": "
                << pluviora_last_error(engine) << '\n';
      continue;
    }

    ++loaded;
    bool skipped_animations = false;
    for (uint32_t index = 0; index < pluviora_warning_count(engine); ++index) {
      const std::string_view warning(pluviora_warning_at(engine, index));
      if (startsWith(warning, "restored ")) {
        ++strict_order;
      } else if (startsWith(warning, "static n(...) ordering hints were incomplete")) {
        ++traversal_order;
      } else if (startsWith(warning, "skipped ")) {
        skipped_animations = true;
      }
    }
    if (skipped_animations) ++charts_with_skipped_animations;
  }

  pluviora_destroy(engine);
  std::cout << "documents=" << documents.size() << " loaded=" << loaded
            << " strict_order=" << strict_order
            << " traversal_order=" << traversal_order
            << " charts_with_skipped_animations="
            << charts_with_skipped_animations << " failed=" << failed
            << '\n';
  return failed == 0 && loaded == documents.size() ? 0 : 1;
}
