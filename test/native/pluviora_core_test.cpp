#include "pluviora.h"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

std::vector<uint8_t> readFile(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error(std::string("cannot open ") + path);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "usage: pluviora_core_test chart.json chart.js font.ttf\n";
    return 2;
  }
  const auto json = readFile(argv[1]);
  const auto js = readFile(argv[2]);
  const auto font = readFile(argv[3]);
  PluvioraHandle engine = pluviora_create();
  assert(engine != nullptr);
  assert(pluviora_set_font_data(engine, font.data(), font.size()) ==
         PLUVIORA_OK);
  const PluvioraStatus load = pluviora_load(
      engine, json.data(), json.size(), js.data(), js.size());
  if (load != PLUVIORA_OK) {
    std::cerr << pluviora_last_error(engine) << '\n';
    return 1;
  }
  PluvioraMetadataView metadata{};
  assert(pluviora_get_metadata(engine, &metadata) == PLUVIORA_OK);
  const std::string title(metadata.title);
  assert(title == "Fixture");
  assert(metadata.line_count == 1);
  assert(metadata.note_count == 2);
  assert(metadata.animation_count == 2);
  assert(metadata.storyboard_count == 1);
  assert(pluviora_warning_count(engine) == 2);
  assert(std::string(pluviora_warning_at(engine, 0)) ==
         "restored 2 note indices from static n(...) ordering hints");
  assert(std::string(pluviora_warning_at(engine, 1)) ==
         "skipped 2 animations without an i1 target");
  const auto render = [&](double time) -> PluvioraFrameView {
    PluvioraFrameView frame{};
    const auto status = pluviora_render(engine, time, 1920, 1080, 160.0, &frame);
    if (status != PLUVIORA_OK) {
      throw std::runtime_error(pluviora_last_error(engine));
    }
    assert(frame.data != nullptr);
    assert(frame.length <= frame.capacity);
    assert(frame.command_count > 0);
    return frame;
  };
  assert(render(0.5).hit_count == 0);
  assert(render(1.0).hit_count == 1);
  assert(render(1.1).hit_count == 0);
  assert(render(0.5).hit_count == 0);
  assert(render(1.0).hit_count == 1);
  assert(render(2.5).hit_count == 1);
  assert(render(2.25).hit_count == 0);
  assert(render(1.5).hit_count == 0);
  assert(render(2.0).hit_count == 1);
  assert(pluviora_destroy(engine) == PLUVIORA_OK);
  assert(pluviora_destroy(nullptr) == PLUVIORA_OK);
  return 0;
}
