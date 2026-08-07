#include "pluviora.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

std::vector<uint8_t> readFile(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error(std::string("cannot open ") + path);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
  if (argc != 4) {
    std::cerr << "usage: pluviora_benchmark chart.json chart.js font.ttf\n";
    return 2;
  }
  const auto json = readFile(argv[1]);
  const auto js = readFile(argv[2]);
  const auto font = readFile(argv[3]);
  PluvioraHandle engine = pluviora_create();
  if (pluviora_set_font_data(engine, font.data(), font.size()) !=
      PLUVIORA_OK) {
    std::cerr << pluviora_last_error(engine) << '\n';
    return 1;
  }
  if (pluviora_load(engine, json.data(), json.size(), js.data(), js.size()) !=
      PLUVIORA_OK) {
    std::cerr << pluviora_last_error(engine) << '\n';
    return 1;
  }
  PluvioraMetadataView metadata{};
  pluviora_get_metadata(engine, &metadata);
  std::vector<double> elapsed_ms;
  uint32_t maximum_capacity = 0;
  const int frame_count = std::max(1, static_cast<int>(metadata.chart_duration * 120));
  elapsed_ms.reserve(frame_count);
  for (int frame_index = 0; frame_index < frame_count; ++frame_index) {
    PluvioraFrameView frame{};
    const double time = static_cast<double>(frame_index) / 120.0;
    const auto start = std::chrono::steady_clock::now();
    const auto status = pluviora_render(
        engine, time, 1920, 1080, metadata.chart_duration, &frame);
    const auto end = std::chrono::steady_clock::now();
    if (status != PLUVIORA_OK || frame.length > frame.capacity) {
      std::cerr << pluviora_last_error(engine) << '\n';
      return 1;
    }
    maximum_capacity = std::max(maximum_capacity, frame.capacity);
    elapsed_ms.push_back(
        std::chrono::duration<double, std::milli>(end - start).count());
  }
  std::sort(elapsed_ms.begin(), elapsed_ms.end());
  const auto percentile = [&](double value) {
    return elapsed_ms[static_cast<size_t>((elapsed_ms.size() - 1) * value)];
  };
  std::cout << metadata.title << " frames=" << frame_count
            << " p50_ms=" << percentile(0.5)
            << " p95_ms=" << percentile(0.95)
            << " p99_ms=" << percentile(0.99)
            << " capacity=" << maximum_capacity << '\n';
  pluviora_destroy(engine);
  return percentile(0.95) <= 2.0 ? 0 : 1;
}
