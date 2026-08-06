#include "pluviora.h"

#include "third_party/yyjson/yyjson.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pluviora {

using std::size_t;

static_assert(std::endian::native == std::endian::little,
              "Pluviora ABI v1 requires a little-endian target");

constexpr double kPi = std::numbers::pi_v<double>;
constexpr uint32_t kInvalidGroup = std::numeric_limits<uint32_t>::max();
constexpr double kWorldWidth = 1920.0;
constexpr double kWorldHeight = 1080.0;
constexpr double kHitDuration = 0.5;

struct Vec2 {
  double x = 0.0;
  double y = 0.0;

  Vec2 operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
  Vec2 operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
  Vec2 operator*(double value) const { return {x * value, y * value}; }
  Vec2 operator/(double value) const { return {x / value, y / value}; }
  Vec2& operator+=(const Vec2& other) {
    x += other.x;
    y += other.y;
    return *this;
  }
  double length() const { return std::hypot(x, y); }
};

struct Color {
  double r = 1.0;
  double g = 1.0;
  double b = 1.0;
  double a = 1.0;

  uint32_t rgba(double alpha = 1.0) const {
    auto channel = [](double value) -> uint32_t {
      return static_cast<uint32_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
    };
    return (channel(r) << 24) | (channel(g) << 16) | (channel(b) << 8) |
           channel(a * alpha);
  }
};

Color lerp(const Color& a, const Color& b, double t) {
  return {
      a.r + (b.r - a.r) * t,
      a.g + (b.g - a.g) * t,
      a.b + (b.b - a.b) * t,
      a.a + (b.a - a.a) * t,
  };
}

Vec2 rotate(const Vec2& point, double degrees) {
  const double radians = degrees * kPi / 180.0;
  const double c = std::cos(radians);
  const double s = std::sin(radians);
  return {point.x * c - point.y * s, point.x * s + point.y * c};
}

uint64_t fnv1a(std::string_view bytes) {
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}

uint64_t mix64(uint64_t value) {
  value += 0x9e3779b97f4a7c15ull;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
  return value ^ (value >> 31);
}

double random01(uint64_t seed) {
  return static_cast<double>(mix64(seed) >> 11) * (1.0 / 9007199254740992.0);
}

double randomRange(uint64_t seed, double low, double high) {
  return low + (high - low) * random01(seed);
}

class ExpressionParser {
 public:
  ExpressionParser(std::string_view source, double t, double x)
      : source_(source), t_(t), x_(x) {}

  static double Evaluate(std::string_view source, double t, double x) {
    ExpressionParser parser(source, t, x);
    const double result = parser.parseExpression();
    parser.skipWhitespace();
    if (parser.position_ != parser.source_.size() || !std::isfinite(result)) {
      throw std::runtime_error("invalid animation expression");
    }
    return result;
  }

 private:
  std::string_view source_;
  size_t position_ = 0;
  double t_;
  double x_;

  void skipWhitespace() {
    while (position_ < source_.size() &&
           std::isspace(static_cast<unsigned char>(source_[position_]))) {
      ++position_;
    }
  }

  bool consume(char expected) {
    skipWhitespace();
    if (position_ >= source_.size() || source_[position_] != expected) return false;
    ++position_;
    return true;
  }

  double parseExpression() {
    double value = parseTerm();
    while (true) {
      if (consume('+')) value += parseTerm();
      else if (consume('-')) value -= parseTerm();
      else return value;
    }
  }

  double parseTerm() {
    double value = parsePower();
    while (true) {
      if (consume('*')) value *= parsePower();
      else if (consume('/')) value /= parsePower();
      else return value;
    }
  }

  double parsePower() {
    double value = parseUnary();
    if (consume('^')) value = std::pow(value, parsePower());
    return value;
  }

  double parseUnary() {
    if (consume('+')) return parseUnary();
    if (consume('-')) return -parseUnary();
    return parsePrimary();
  }

  double parsePrimary() {
    skipWhitespace();
    if (consume('(')) {
      double value = parseExpression();
      if (!consume(')')) fail();
      return value;
    }

    if (position_ < source_.size() &&
        (std::isdigit(static_cast<unsigned char>(source_[position_])) ||
         source_[position_] == '.')) {
      std::string suffix(source_.substr(position_));
      char* end = nullptr;
      const double value = std::strtod(suffix.c_str(), &end);
      if (end == suffix.c_str()) fail();
      position_ += static_cast<size_t>(end - suffix.c_str());
      return value;
    }

    const std::string name = parseIdentifier();
    if (name == "t") return t_;
    if (name == "x") return x_;
    if (name == "PI") return kPi;

    if (!consume('(')) fail();
    const double first = parseExpression();
    std::optional<double> second;
    if (consume(',')) second = parseExpression();
    if (!consume(')')) fail();

    if (name == "sin") return std::sin(first);
    if (name == "cos") return std::cos(first);
    if (name == "tan") return std::tan(first);
    if (name == "sqrt") return std::sqrt(first);
    if (name == "abs") return std::abs(first);
    if (name == "exp") return std::exp(first);
    if (name == "log") return std::log(first);
    if (name == "floor") return std::floor(first);
    if (name == "ceil") return std::ceil(first);
    if (name == "pow" && second) return std::pow(first, *second);
    if (name == "min" && second) return std::min(first, *second);
    if (name == "max" && second) return std::max(first, *second);
    fail();
  }

  std::string parseIdentifier() {
    skipWhitespace();
    const size_t start = position_;
    while (position_ < source_.size()) {
      const unsigned char c = static_cast<unsigned char>(source_[position_]);
      if (!std::isalnum(c) && c != '_') break;
      ++position_;
    }
    if (start == position_) fail();
    return std::string(source_.substr(start, position_ - start));
  }

  [[noreturn]] void fail() const {
    throw std::runtime_error("invalid animation expression near offset " +
                             std::to_string(position_));
  }
};

double bounceOut(double x) {
  constexpr double n1 = 7.5625;
  constexpr double d1 = 2.75;
  if (x < 1.0 / d1) return n1 * x * x;
  if (x < 2.0 / d1) {
    x -= 1.5 / d1;
    return n1 * x * x + 0.75;
  }
  if (x < 2.5 / d1) {
    x -= 2.25 / d1;
    return n1 * x * x + 0.9375;
  }
  x -= 2.625 / d1;
  return n1 * x * x + 0.984375;
}

double easeIn(uint32_t function, double x) {
  switch (function) {
    case 0: return x;
    case 1: return 1.0 - std::cos(x * kPi / 2.0);
    case 2: return x * x;
    case 3: return x * x * x;
    case 4: return x * x * x * x;
    case 5: return x * x * x * x * x;
    case 6: return x == 0.0 ? 0.0 : std::pow(2.0, 10.0 * x - 10.0);
    case 7: return 1.0 - std::sqrt(std::max(0.0, 1.0 - x * x));
    case 8: {
      constexpr double c1 = 1.70158;
      constexpr double c3 = c1 + 1.0;
      return c3 * x * x * x - c1 * x * x;
    }
    case 9: {
      if (x == 0.0 || x == 1.0) return x;
      constexpr double c4 = 2.0 * kPi / 3.0;
      return -std::pow(2.0, 10.0 * x - 10.0) *
             std::sin((x * 10.0 - 10.75) * c4);
    }
    case 10: return 1.0 - bounceOut(1.0 - x);
    case 13: return x * x * (3.0 - 2.0 * x);
    case 14: return std::sqrt(x);
    case 15: return std::sqrt(std::sqrt(x));
    default: return x;
  }
}

double bezierCoordinate(double t, double p1, double p2) {
  const double mt = 1.0 - t;
  return 3.0 * mt * mt * t * p1 + 3.0 * mt * t * t * p2 + t * t * t;
}

double bezierAtX(double x, uint32_t direction) {
  double x1 = 0.42;
  double y1 = 0.0;
  double x2 = 1.0;
  double y2 = 1.0;
  if (direction == 1) {
    x1 = 0.0;
    x2 = 0.58;
  } else if (direction == 2) {
    x2 = 0.58;
  }
  double low = 0.0;
  double high = 1.0;
  for (int i = 0; i < 24; ++i) {
    const double t = (low + high) * 0.5;
    if (bezierCoordinate(t, x1, x2) < x) low = t;
    else high = t;
  }
  return bezierCoordinate((low + high) * 0.5, y1, y2);
}

double easing(uint32_t function, uint32_t direction, double x) {
  x = std::clamp(x, 0.0, 1.0);
  if (function == 11) return bezierAtX(x, direction);
  if (function == 12) return x;
  if (direction == 0) return easeIn(function, x);
  if (direction == 1) return 1.0 - easeIn(function, 1.0 - x);
  if (x < 0.5) return easeIn(function, 2.0 * x) * 0.5;
  return 1.0 - easeIn(function, 2.0 - 2.0 * x) * 0.5;
}

enum class ObjectType : uint8_t { line = 0, note = 1, storyboard = 2 };
enum class NoteType : uint8_t { hit = 0, drag = 1, fracture = 2 };
enum class StoryboardType : uint8_t { picture = 0, text = 1 };
enum class StoryboardLayer : uint8_t { background = 0, normal = 1, foreground = 2 };

double defaultValue(ObjectType object, uint32_t type) {
  if (object == ObjectType::line) {
    switch (type) {
      case 1: return -350.0;
      case 2: return 1.0;
      case 3: return 1.0;
      case 4: return 90.0;
      case 5: return 9.0;
      case 8: return 1.0;
      case 9: return 1.0;
      case 12: return 1.0;
      case 13: return 1.0;
      case 23: return std::hypot(kWorldWidth, kWorldHeight) * 1.5;
      default: return 0.0;
    }
  }
  if (object == ObjectType::note) {
    switch (type) {
      case 2: return 1.0;
      case 3: return 1.0;
      case 5: return 1.0;
      default: return 0.0;
    }
  }
  switch (type) {
    case 2: return 1.0;
    case 3: return 1.0;
    case 10: return 1.0;
    case 11: return 1.0;
    case 14: return -0.5;
    case 15: return -0.5;
    case 16: return 0.5;
    case 17: return -0.5;
    case 18: return -0.5;
    case 19: return 0.5;
    case 20: return 0.5;
    case 21: return 0.5;
    default: return 0.0;
  }
}

struct Event {
  double start = 0.0;
  double end = 0.0;
  double from = 0.0;
  double to = 0.0;
  uint32_t function = 0;
  uint32_t direction = 0;
  uint32_t index = 0;
  double cumulative = 0.0;
  std::vector<double> samples;

  double progress(double time) const {
    if (start == end) return 1.0;
    return std::clamp((time - start) / (end - start), 0.0, 1.0);
  }

  double value(double time) const {
    double p = progress(time);
    if (!samples.empty()) {
      if (samples.size() == 1) return samples.front();
      const double cursor = p * static_cast<double>(samples.size() - 1);
      const size_t index = std::min(static_cast<size_t>(cursor), samples.size() - 1);
      const size_t next = std::min(index + 1, samples.size() - 1);
      return samples[index] + (samples[next] - samples[index]) * (cursor - index);
    }
    p = easing(function, direction, p);
    return from + (to - from) * p;
  }

  double integral(double time) const {
    if (start == end) {
      if (time > end) return to * (time - end);
      if (time < start) return -from * (start - time);
      return 0.0;
    }
    const double p = progress(time);
    constexpr int steps = 24;
    double sum = 0.0;
    for (int i = 0; i <= steps; ++i) {
      const double sampleP = p * static_cast<double>(i) / steps;
      const double sampleTime = start + (end - start) * sampleP;
      const double weight = (i == 0 || i == steps) ? 1.0 : (i % 2 ? 4.0 : 2.0);
      sum += value(sampleTime) * weight;
    }
    double result = (end - start) * p * sum / (3.0 * steps);
    if (time > end) result += to * (time - end);
    if (time < start) result -= from * (start - time);
    return result;
  }
};

struct EventTrack {
  std::vector<Event> events;
  size_t current = 0;
  double last_time = -std::numeric_limits<double>::infinity();

  void initialize(bool speed) {
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
      if (a.start != b.start) return a.start < b.start;
      if (a.end != b.end) return a.end < b.end;
      return a.index < b.index;
    });
    current = 0;
    last_time = -std::numeric_limits<double>::infinity();
    if (!speed || events.empty()) return;
    double cumulative = events.front().start * events.front().from;
    for (size_t i = 0; i < events.size(); ++i) {
      events[i].cumulative = cumulative;
      if (i + 1 < events.size()) cumulative += events[i].integral(events[i + 1].start);
    }
  }

  Event& at(double time) {
    if (time < last_time) current = 0;
    while (current + 1 < events.size() && events[current].end <= time &&
           events[current + 1].start <= time) {
      ++current;
    }
    last_time = time;
    return events[current];
  }
};

struct AnimationGroup {
  ObjectType type = ObjectType::line;
  std::array<EventTrack, 24> tracks;

  double get(uint32_t key, double time) {
    EventTrack& track = tracks[key];
    if (track.events.empty()) {
      const double value = defaultValue(type, key);
      return key == 12 ? value * time : value;
    }
    Event& event = track.at(time);
    return key == 12 ? event.cumulative + event.integral(time) : event.value(time);
  }

  std::pair<double, double> zone(uint32_t key, double time) {
    EventTrack& track = tracks[key];
    if (track.events.empty()) {
      const double value = defaultValue(type, key);
      return {value, value};
    }
    Event& event = track.at(time);
    return {event.from, event.to};
  }

  bool has(uint32_t key) const { return !tracks[key].events.empty(); }
};

struct Metadata {
  std::string title;
  std::string composer;
  std::string illustrator;
  std::string beatmapper;
  std::string difficulty;
  std::string audio_file;
  std::string illustration_file;
  double difficulty_value = 0.0;
};

struct Note {
  uint32_t group = kInvalidGroup;
  uint64_t logical_index = 0;
  NoteType type = NoteType::hit;
  double start = 0.0;
  double end = 0.0;
  bool fake = false;
  bool always_perfect = false;
  bool simultaneous = false;
  double floor_start = 0.0;
  double floor_end = 0.0;
  uint32_t line = 0;

  bool hold() const { return type == NoteType::hit && start != end; }
};

struct Line {
  uint32_t group = kInvalidGroup;
  std::vector<Note> notes;
};

struct Storyboard {
  uint32_t group = kInvalidGroup;
  StoryboardType type = StoryboardType::picture;
  StoryboardLayer layer = StoryboardLayer::normal;
  std::string data;
};

struct Chart {
  Metadata meta;
  std::vector<Line> lines;
  std::vector<Storyboard> storyboards;
  std::vector<AnimationGroup> groups;
  std::unordered_map<uint64_t, uint32_t> group_lookup;
  std::vector<Color> colors;
  std::vector<double> combo_times;
  uint64_t content_hash = 0;
  uint32_t animation_count = 0;
  uint32_t note_count = 0;
  double duration = 0.0;
  double note_scale = 1.0;
  double flow_speed = 1.66;

  uint32_t ensureGroup(ObjectType type, uint64_t logical_index) {
    const uint64_t key = (static_cast<uint64_t>(type) << 61) ^ logical_index;
    const auto found = group_lookup.find(key);
    if (found != group_lookup.end()) return found->second;
    const uint32_t index = static_cast<uint32_t>(groups.size());
    groups.emplace_back().type = type;
    group_lookup.emplace(key, index);
    return index;
  }

  double get(uint32_t group, uint32_t key, double time) {
    return groups[group].get(key, time);
  }

  std::pair<double, double> zone(uint32_t group, uint32_t key, double time) {
    return groups[group].zone(key, time);
  }

  bool has(uint32_t group, uint32_t key) const { return groups[group].has(key); }

  Color color(uint32_t group, double time) {
    const double value = get(group, 22, time);
    const auto [from, to] = zone(group, 22, time);
    if (from < 1.0 || colors.empty()) return {};
    const size_t first = std::min(static_cast<size_t>(from - 1.0), colors.size() - 1);
    const size_t second = std::min(static_cast<size_t>(to - 1.0), colors.size() - 1);
    return lerp(colors[first], colors[second], std::clamp(value - from, 0.0, 1.0));
  }

  Vec2 position(uint32_t group, double time, float width, float height) {
    const double x = get(group, 0, time) + get(group, 6, time);
    const double y = get(group, 1, time) + get(group, 7, time);
    return {(x + kWorldWidth * 0.5) / kWorldWidth * width,
            (kWorldHeight * 0.5 - y) / kWorldHeight * height};
  }
};

yyjson_val* required(yyjson_val* object, const char* key) {
  yyjson_val* value = yyjson_obj_get(object, key);
  if (!value) throw std::runtime_error(std::string("missing ") + key);
  return value;
}

double number(yyjson_val* value, const char* context) {
  if (yyjson_is_num(value)) return yyjson_get_num(value);
  if (yyjson_is_str(value)) {
    const char* text = yyjson_get_str(value);
    char* end = nullptr;
    const double result = std::strtod(text, &end);
    if (end && *end == '\0') return result;
  }
  throw std::runtime_error(std::string(context) + " is not numeric");
}

uint64_t integer(yyjson_val* value, const char* context) {
  const double parsed = number(value, context);
  if (!std::isfinite(parsed) || parsed < 0.0 || std::floor(parsed) != parsed) {
    throw std::runtime_error(std::string(context) + " is not a non-negative integer");
  }
  return static_cast<uint64_t>(parsed);
}

bool boolean(yyjson_val* value, const char* context) {
  if (!yyjson_is_bool(value)) throw std::runtime_error(std::string(context) + " is not bool");
  return yyjson_get_bool(value);
}

std::string string(yyjson_val* value, const char* context) {
  if (!yyjson_is_str(value)) throw std::runtime_error(std::string(context) + " is not string");
  return std::string(yyjson_get_str(value), yyjson_get_len(value));
}

std::vector<uint64_t> extractNoteOrder(std::string_view source) {
  std::vector<uint64_t> result;
  auto identifier = [](char c) {
    const unsigned char byte = static_cast<unsigned char>(c);
    return std::isalnum(byte) || c == '_' || c == '$';
  };
  for (size_t i = 0; i < source.size();) {
    const char c = source[i];
    if (c == '\'' || c == '"' || c == '`') {
      const char quote = c;
      ++i;
      while (i < source.size()) {
        if (source[i] == '\\') {
          i = std::min(i + 2, source.size());
          continue;
        }
        if (source[i++] == quote) break;
      }
      continue;
    }
    if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
      i += 2;
      while (i < source.size() && source[i] != '\n') ++i;
      continue;
    }
    if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
      i += 2;
      while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == '/')) ++i;
      i = std::min(i + 2, source.size());
      continue;
    }
    if (c != 'n' || (i > 0 && identifier(source[i - 1]))) {
      ++i;
      continue;
    }
    size_t cursor = i + 1;
    while (cursor < source.size() && std::isspace(static_cast<unsigned char>(source[cursor]))) ++cursor;
    if (cursor >= source.size() || source[cursor] != '(') {
      ++i;
      continue;
    }
    ++cursor;
    while (cursor < source.size() && std::isspace(static_cast<unsigned char>(source[cursor]))) ++cursor;
    const size_t start = cursor;
    while (cursor < source.size() && std::isdigit(static_cast<unsigned char>(source[cursor]))) ++cursor;
    if (start == cursor) {
      ++i;
      continue;
    }
    const size_t end = cursor;
    while (cursor < source.size() && std::isspace(static_cast<unsigned char>(source[cursor]))) ++cursor;
    if (cursor >= source.size() || source[cursor] != ',') {
      ++i;
      continue;
    }
    uint64_t line = 0;
    const auto parsed = std::from_chars(source.data() + start, source.data() + end, line);
    if (parsed.ec == std::errc()) result.push_back(line);
    i = cursor + 1;
  }
  return result;
}

Color parseColor(std::string_view value) {
  auto hexadecimal = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  if (!value.empty() && value.front() == '#') {
    value.remove_prefix(1);
    if (value.size() == 3 || value.size() == 4) {
      std::array<int, 4> channels{15, 15, 15, 15};
      for (size_t i = 0; i < value.size(); ++i) channels[i] = hexadecimal(value[i]);
      for (int channel : channels) if (channel < 0) throw std::runtime_error("invalid color");
      return {channels[0] / 15.0, channels[1] / 15.0, channels[2] / 15.0,
              channels[3] / 15.0};
    }
    if (value.size() == 6 || value.size() == 8) {
      std::array<int, 4> channels{255, 255, 255, 255};
      for (size_t i = 0; i < value.size() / 2; ++i) {
        channels[i] = hexadecimal(value[i * 2]) * 16 + hexadecimal(value[i * 2 + 1]);
        if (channels[i] < 0) throw std::runtime_error("invalid color");
      }
      return {channels[0] / 255.0, channels[1] / 255.0, channels[2] / 255.0,
              channels[3] / 255.0};
    }
  }
  if (value == "white") return {};
  if (value == "black") return {0.0, 0.0, 0.0, 1.0};
  if (value.starts_with("oklch(") && value.ends_with(')')) {
    std::string components(value.substr(6, value.size() - 7));
    std::replace(components.begin(), components.end(), '/', ' ');
    std::istringstream stream(components);
    double lightness = 0.0;
    double chroma = 0.0;
    double hue = 0.0;
    double alpha = 1.0;
    if (!(stream >> lightness >> chroma >> hue)) {
      throw std::runtime_error("invalid oklch color");
    }
    stream >> alpha;
    const double radians = hue * kPi / 180.0;
    const double lab_a = chroma * std::cos(radians);
    const double lab_b = chroma * std::sin(radians);
    const double l_root = lightness + 0.3963377774 * lab_a + 0.2158037573 * lab_b;
    const double m_root = lightness - 0.1055613458 * lab_a - 0.0638541728 * lab_b;
    const double s_root = lightness - 0.0894841775 * lab_a - 1.2914855480 * lab_b;
    const double l = l_root * l_root * l_root;
    const double m = m_root * m_root * m_root;
    const double s = s_root * s_root * s_root;
    auto to_srgb = [](double linear) {
      const double encoded = linear <= 0.0031308
                                 ? 12.92 * linear
                                 : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
      return std::clamp(encoded, 0.0, 1.0);
    };
    return {
        to_srgb(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s),
        to_srgb(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s),
        to_srgb(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s),
        std::clamp(alpha, 0.0, 1.0),
    };
  }
  try {
    const uint64_t parsed = std::stoull(std::string(value));
    return {
        static_cast<double>((parsed >> 24) & 0xff) / 255.0,
        static_cast<double>((parsed >> 16) & 0xff) / 255.0,
        static_cast<double>((parsed >> 8) & 0xff) / 255.0,
        static_cast<double>(parsed & 0xff) / 255.0,
    };
  } catch (...) {
    throw std::runtime_error("invalid color value");
  }
}

struct BpmEvent {
  double start = 0.0;
  double bpm = 0.0;
};

double parseTime(yyjson_val* object, const char* key, double bpm_reference,
                 const std::vector<BpmEvent>& bpms) {
  yyjson_val* value = required(object, key);
  if (yyjson_is_num(value)) return yyjson_get_num(value);
  if (!yyjson_is_arr(value)) throw std::runtime_error(std::string("invalid ") + key);
  const size_t count = yyjson_arr_size(value);
  if (count != 3 && count != 4) throw std::runtime_error(std::string("invalid ") + key);
  yyjson_val* item0 = yyjson_arr_get(value, 0);
  yyjson_val* item1 = yyjson_arr_get(value, 1);
  yyjson_val* item2 = yyjson_arr_get(value, 2);
  const double division = number(item2, key);
  const double reference = count == 4 ? number(yyjson_arr_get(value, 3), key) : bpm_reference;
  size_t bpm_index = std::numeric_limits<size_t>::max();
  if (reference >= 0.0 && std::floor(reference) == reference && reference < bpms.size()) {
    bpm_index = static_cast<size_t>(reference);
  } else {
    for (size_t i = 0; i < bpms.size(); ++i) {
      if (std::abs(bpms[i].bpm - reference) <= std::max(1.0, std::abs(reference)) * 1e-9) {
        bpm_index = i;
        break;
      }
    }
  }
  if (division == 0.0 || bpm_index == std::numeric_limits<size_t>::max() ||
      bpms[bpm_index].bpm == 0.0) {
    throw std::runtime_error(std::string("invalid BPM reference in ") + key);
  }
  const double beats = number(item0, key) + number(item1, key) / division;
  return bpms[bpm_index].start + beats * 60.0 / bpms[bpm_index].bpm;
}

Chart parseChart(std::string_view json, std::string_view js, std::vector<std::string>& warnings) {
  yyjson_read_err read_error{};
  yyjson_doc* raw_doc = yyjson_read_opts(const_cast<char*>(json.data()), json.size(),
                                         YYJSON_READ_NOFLAG, nullptr, &read_error);
  if (!raw_doc) {
    throw std::runtime_error("JSON parse error at byte " + std::to_string(read_error.pos) +
                             ": " + (read_error.msg ? read_error.msg : "unknown error"));
  }
  std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(raw_doc, yyjson_doc_free);
  yyjson_val* root = yyjson_doc_get_root(doc.get());
  if (!yyjson_is_obj(root)) throw std::runtime_error("root is not an object");

  Chart chart;
  chart.content_hash = fnv1a(json);
  yyjson_val* meta = required(root, "meta");
  if (!yyjson_is_obj(meta)) throw std::runtime_error("meta is not an object");
  chart.meta.title = string(required(meta, "Title"), "Title");
  chart.meta.composer = string(required(meta, "Composer"), "Composer");
  chart.meta.illustrator = string(required(meta, "Illustrator"), "Illustrator");
  chart.meta.beatmapper = string(required(meta, "Beatmapper"), "Beatmapper");
  chart.meta.difficulty = string(required(meta, "Difficulty"), "Difficulty");
  chart.meta.difficulty_value = number(required(meta, "DifficultyValue"), "DifficultyValue");
  if (yyjson_val* value = yyjson_obj_get(meta, "AudioFile"); yyjson_is_str(value)) {
    chart.meta.audio_file = string(value, "AudioFile");
  }
  if (yyjson_val* value = yyjson_obj_get(meta, "IllustrationFile"); yyjson_is_str(value)) {
    chart.meta.illustration_file = string(value, "IllustrationFile");
  }

  yyjson_val* bpm_array = required(root, "bpms");
  if (!yyjson_is_arr(bpm_array)) throw std::runtime_error("bpms is not an array");
  std::vector<BpmEvent> bpms;
  size_t bpm_i, bpm_max;
  yyjson_val* bpm_value;
  yyjson_arr_foreach(bpm_array, bpm_i, bpm_max, bpm_value) {
    if (!yyjson_is_obj(bpm_value)) throw std::runtime_error("bpm is not an object");
    bpms.push_back({number(required(bpm_value, "start"), "bpm.start"),
                    number(required(bpm_value, "bpm"), "bpm.bpm")});
  }

  yyjson_val* line_array = required(root, "lines");
  if (!yyjson_is_arr(line_array)) throw std::runtime_error("lines is not an array");
  const std::vector<uint64_t> order = js.empty() ? std::vector<uint64_t>{} : extractNoteOrder(js);
  if (js.empty()) warnings.emplace_back("未提供顺序提示文件，将使用 JSON 分组顺序。");

  std::vector<std::vector<uint64_t>> order_by_line(yyjson_arr_size(line_array));
  bool order_valid = !order.empty();
  if (order_valid) {
    for (size_t i = 0; i < order.size(); ++i) {
      if (order[i] >= order_by_line.size()) {
        order_valid = false;
        break;
      }
      order_by_line[order[i]].push_back(i);
    }
  }
  if (order_valid) {
    size_t check_i, check_max;
    yyjson_val* check_line;
    yyjson_arr_foreach(line_array, check_i, check_max, check_line) {
      yyjson_val* notes = yyjson_is_obj(check_line)
                              ? yyjson_obj_get(check_line, "notes")
                              : nullptr;
      if (!yyjson_is_arr(notes) ||
          yyjson_arr_size(notes) != order_by_line[check_i].size()) {
        order_valid = false;
        break;
      }
    }
  }

  size_t line_i, line_max;
  yyjson_val* line_value;
  uint64_t fallback_note_index = 0;
  yyjson_arr_foreach(line_array, line_i, line_max, line_value) {
    if (!yyjson_is_obj(line_value)) throw std::runtime_error("line is not an object");
    yyjson_val* notes = required(line_value, "notes");
    if (!yyjson_is_arr(notes)) throw std::runtime_error("notes is not an array");
    Line& line = chart.lines.emplace_back();
    line.group = chart.ensureGroup(ObjectType::line, line_i);
    size_t note_i, note_max;
    yyjson_val* note_value;
    yyjson_arr_foreach(notes, note_i, note_max, note_value) {
      if (!yyjson_is_obj(note_value)) throw std::runtime_error("note is not an object");
      Note& note = line.notes.emplace_back();
      const double bpm = number(required(note_value, "bpm"), "note.bpm");
      note.start = parseTime(note_value, "startTime", bpm, bpms);
      note.end = parseTime(note_value, "endTime", bpm, bpms);
      const uint64_t type = integer(required(note_value, "type"), "note.type");
      if (type > 2) throw std::runtime_error("unsupported note type");
      note.type = static_cast<NoteType>(type);
      note.fake = boolean(required(note_value, "isFake"), "note.isFake");
      note.always_perfect = boolean(required(note_value, "isAlwaysPerfect"), "note.isAlwaysPerfect");
      note.line = static_cast<uint32_t>(line_i);
      note.logical_index = fallback_note_index++;
      if (order_valid) note.logical_index = order_by_line[line_i][note_i];
      if (yyjson_val* index = yyjson_obj_get(note_value, "index")) {
        note.logical_index = integer(index, "note.index");
      }
      note.group = chart.ensureGroup(ObjectType::note, note.logical_index);
      chart.duration = std::max(chart.duration, note.end);
      ++chart.note_count;
    }
  }
  if (!js.empty() && !order_valid) {
    warnings.emplace_back("同名 JS 的 n(line, ...) 顺序与 JSON 不匹配，已安全回退到 JSON 顺序。");
  }

  yyjson_val* storyboard_array = required(root, "storyboardObjects");
  if (!yyjson_is_arr(storyboard_array)) throw std::runtime_error("storyboardObjects is not an array");
  size_t storyboard_i, storyboard_max;
  yyjson_val* storyboard_value;
  yyjson_arr_foreach(storyboard_array, storyboard_i, storyboard_max, storyboard_value) {
    if (!yyjson_is_obj(storyboard_value)) throw std::runtime_error("storyboard is not an object");
    Storyboard& storyboard = chart.storyboards.emplace_back();
    const uint64_t type = integer(required(storyboard_value, "type"), "storyboard.type");
    const uint64_t layer = integer(required(storyboard_value, "layer"), "storyboard.layer");
    if (type > 1 || layer > 2) throw std::runtime_error("unsupported storyboard type or layer");
    storyboard.type = static_cast<StoryboardType>(type);
    storyboard.layer = static_cast<StoryboardLayer>(layer);
    storyboard.data = string(required(storyboard_value, "data"), "storyboard.data");
    storyboard.group = chart.ensureGroup(ObjectType::storyboard, storyboard_i);
  }

  yyjson_val* animation_array = required(root, "animations");
  if (!yyjson_is_arr(animation_array)) throw std::runtime_error("animations is not an array");
  size_t animation_i, animation_max;
  yyjson_val* animation_value;
  yyjson_arr_foreach(animation_array, animation_i, animation_max, animation_value) {
    if (!yyjson_is_obj(animation_value)) throw std::runtime_error("animation is not an object");
    const uint64_t key = integer(required(animation_value, "key"), "animation.key");
    const uint64_t target = integer(required(animation_value, "data"), "animation.data");
    if (key > 23 || target > 2) throw std::runtime_error("unsupported animation key or target");
    yyjson_val* target_index = yyjson_obj_get(animation_value, "i1");
    if (!target_index || !yyjson_is_num(target_index) || yyjson_get_num(target_index) < 0.0) {
      warnings.emplace_back("已跳过缺少有效 i1 的动画 #" + std::to_string(animation_i) + "。");
      continue;
    }
    const uint64_t logical_index = integer(target_index, "animation.i1");
    const ObjectType object_type = static_cast<ObjectType>(target);
    const uint32_t group = chart.ensureGroup(object_type, logical_index);
    Event event;
    event.index = static_cast<uint32_t>(animation_i);
    const double bpm = number(required(animation_value, "bpmId"), "animation.bpmId");
    event.start = parseTime(animation_value, "fromBeat", bpm, bpms);
    event.end = parseTime(animation_value, "toBeat", bpm, bpms);
    event.function = static_cast<uint32_t>(integer(required(animation_value, "press"), "animation.press"));
    event.direction = static_cast<uint32_t>(integer(required(animation_value, "ease"), "animation.ease"));
    if (event.function > 15 || event.direction > 2) throw std::runtime_error("unsupported easing");
    if (key == 22) {
      const std::string from = string(required(animation_value, "fv"), "animation.fv");
      const std::string to = string(required(animation_value, "tv"), "animation.tv");
      chart.colors.push_back(parseColor(from));
      event.from = static_cast<double>(chart.colors.size());
      chart.colors.push_back(parseColor(to));
      event.to = static_cast<double>(chart.colors.size());
    } else {
      event.from = number(required(animation_value, "fv"), "animation.fv");
      event.to = number(required(animation_value, "tv"), "animation.tv");
    }

    bool value_expression = false;
    if (yyjson_val* value = yyjson_obj_get(animation_value, "valueExpression")) {
      value_expression = boolean(value, "animation.valueExpression");
    }
    if (yyjson_val* custom = yyjson_obj_get(animation_value, "isCustomEase");
        custom && boolean(custom, "animation.isCustomEase")) {
      yyjson_val* samples = required(animation_value, "customEaseArr");
      if (!yyjson_is_arr(samples)) throw std::runtime_error("customEaseArr is not an array");
      size_t sample_i, sample_max;
      yyjson_val* sample;
      yyjson_arr_foreach(samples, sample_i, sample_max, sample) {
        event.samples.push_back(number(sample, "customEaseArr item"));
      }
    }
    if (value_expression || event.function == 12) {
      yyjson_val* expression_value = required(animation_value, "customEaseExpression");
      const std::string expression = string(expression_value, "customEaseExpression");
      if (!expression.empty()) {
        event.samples.clear();
        event.samples.reserve(1025);
        for (size_t i = 0; i <= 1024; ++i) {
          const double t = static_cast<double>(i) / 1024.0;
          const double x = event.from + (event.to - event.from) * t;
          const double value = ExpressionParser::Evaluate(expression, t, x);
          event.samples.push_back(value_expression ? value : event.from + (event.to - event.from) * value);
        }
      }
    }
    chart.groups[group].tracks[key].events.push_back(std::move(event));
    chart.duration = std::max(chart.duration, event.end);
    ++chart.animation_count;
  }

  for (AnimationGroup& group : chart.groups) {
    for (uint32_t key = 0; key < group.tracks.size(); ++key) {
      group.tracks[key].initialize(key == 12);
    }
  }

  std::unordered_map<uint64_t, uint32_t> simultaneous;
  for (const Line& line : chart.lines) {
    for (const Note& note : line.notes) {
      uint64_t bits = 0;
      static_assert(sizeof(bits) == sizeof(note.start));
      std::memcpy(&bits, &note.start, sizeof(bits));
      ++simultaneous[bits];
    }
  }
  for (Line& line : chart.lines) {
    std::stable_sort(line.notes.begin(), line.notes.end(),
                     [](const Note& a, const Note& b) { return a.start < b.start; });
    for (Note& note : line.notes) {
      uint64_t bits = 0;
      std::memcpy(&bits, &note.start, sizeof(bits));
      note.simultaneous = simultaneous[bits] > 1;
      note.floor_start = chart.get(line.group, 12, note.start) + chart.get(note.group, 12, note.start);
      note.floor_end = chart.get(line.group, 12, note.end) + chart.get(note.group, 12, note.end);
      if (!note.fake && note.type != NoteType::fracture) {
        chart.combo_times.push_back(note.start);
        if (note.hold()) chart.combo_times.push_back(note.end);
      }
    }
  }
  std::sort(chart.combo_times.begin(), chart.combo_times.end());
  return chart;
}

struct CommandHeader {
  uint16_t type;
  uint16_t flags;
  uint32_t length;
};
static_assert(sizeof(CommandHeader) == 8);

struct RectPayload {
  float x, y, width, height, rotation;
  uint32_t rgba;
};
struct QuadPayload {
  float x1, y1, x2, y2, x3, y3, x4, y4;
  uint32_t rgba;
};
struct SpritePayload {
  float x, y, width, height, rotation;
  uint32_t rgba;
  uint32_t kind;
};
struct NotePayload {
  float x, y, rotation, width, head, body, tail;
  uint32_t rgba;
  uint32_t kind;
  uint32_t note_flags;
};
struct TextPayload {
  float x, y, font_size, rotation, scale_x, scale_y, anchor_x, anchor_y;
  uint32_t rgba;
  uint32_t text_length;
};
struct StoryboardImagePayload {
  float x1, y1, x2, y2, x3, y3, x4, y4;
  uint32_t rgba;
  uint32_t builtin;
  uint32_t name_length;
};
struct HitRingPayload {
  float x, y, size, progress, rotation;
  uint32_t rgba;
};
struct ParticlePayload {
  float x, y, radius_x, radius_y, rotation;
  uint32_t rgba;
};
static_assert(sizeof(RectPayload) == 24);
static_assert(sizeof(QuadPayload) == 36);
static_assert(sizeof(SpritePayload) == 28);
static_assert(sizeof(NotePayload) == 40);
static_assert(sizeof(TextPayload) == 40);
static_assert(sizeof(StoryboardImagePayload) == 44);
static_assert(sizeof(HitRingPayload) == 24);
static_assert(sizeof(ParticlePayload) == 24);

class CommandBuffer {
 public:
  void clear() {
    bytes_.clear();
    count_ = 0;
  }

  template <typename Payload>
  void add(uint16_t type, uint16_t flags, const Payload& payload) {
    static_assert(std::is_trivially_copyable_v<Payload>);
    CommandHeader header{type, flags,
                         static_cast<uint32_t>(sizeof(CommandHeader) + sizeof(Payload))};
    append(header);
    append(payload);
    align();
    ++count_;
  }

  template <typename Payload>
  void addString(uint16_t type, uint16_t flags, const Payload& payload, std::string_view string) {
    static_assert(std::is_trivially_copyable_v<Payload>);
    CommandHeader header{type, flags, static_cast<uint32_t>(
                                          sizeof(CommandHeader) + sizeof(Payload) + string.size())};
    append(header);
    append(payload);
    const auto* begin = reinterpret_cast<const uint8_t*>(string.data());
    bytes_.insert(bytes_.end(), begin, begin + string.size());
    align();
    ++count_;
  }

  const uint8_t* data() const { return bytes_.data(); }
  uint32_t size() const { return static_cast<uint32_t>(bytes_.size()); }
  uint32_t capacity() const { return static_cast<uint32_t>(bytes_.capacity()); }
  uint32_t count() const { return count_; }

 private:
  std::vector<uint8_t> bytes_;
  uint32_t count_ = 0;

  template <typename T>
  void append(const T& value) {
    const auto* begin = reinterpret_cast<const uint8_t*>(&value);
    bytes_.insert(bytes_.end(), begin, begin + sizeof(T));
  }

  void align() {
    while (bytes_.size() % 4 != 0) bytes_.push_back(0);
  }
};

struct NoteFrame {
  Vec2 head;
  Vec2 tail;
  double rotation = 0.0;
  double speed_rotation = 0.0;
  double scale = 1.0;
  double alpha = 1.0;
  Color color;
};

struct Engine {
  std::optional<Chart> chart;
  CommandBuffer commands;
  std::vector<std::string> warnings;
  std::unordered_map<std::string, Vec2> storyboard_asset_sizes;
  std::string last_error;
  double last_render_time = std::numeric_limits<double>::quiet_NaN();

  NoteFrame noteFrame(Line& line, Note& note, double time, float width, float height) {
    Chart& value = *chart;
    const Vec2 line_position = value.position(line.group, time, width, height);
    const double line_rotation = value.get(line.group, 4, time);
    const double line_flow = value.get(line.group, 5, time);
    const double line_size = value.get(line.group, 3, time);
    const double whole_alpha = value.get(line.group, 13, time);
    const double visible_area = value.get(line.group, 23, time);
    const double note_size = value.get(note.group, 3, time);
    const double note_alpha = value.get(note.group, 2, time);
    const double note_rotation = value.get(note.group, 4, time);

    double final_flow = line_flow;
    if (value.has(note.group, 5)) final_flow = value.get(note.group, 5, time);
    const double current_floor = value.get(line.group, 12, std::min(time, note.end)) +
                                 value.get(note.group, 12, std::min(time, note.end));
    Vec2 floor{(note.floor_start - current_floor) * final_flow * 108.0 * value.flow_speed,
               (note.floor_end - current_floor) * final_flow * 108.0 * value.flow_speed};
    if (time >= note.start) floor.x = 0.0;
    double alpha = whole_alpha * note_alpha;
    if (floor.x > visible_area) alpha = 0.0;
    if (value.has(note.group, 1)) {
      floor.y -= floor.x;
      floor.x = value.get(note.group, 1, time);
      floor.y += floor.x;
    }
    const Vec2 base{value.get(note.group, 6, time) + value.get(note.group, 0, time),
                    value.get(note.group, 7, time)};
    const double transform_rotation = 90.0 - line_rotation;
    auto transform = [&](Vec2 point) {
      point.y *= -1.0;
      point.x *= width / kWorldWidth * line_size;
      point.y *= height / kWorldHeight * line_size;
      return line_position + rotate(point, transform_rotation);
    };
    NoteFrame frame;
    frame.head = transform(base + Vec2{0.0, floor.x});
    frame.tail = transform(base + Vec2{0.0, floor.y});
    frame.rotation = -line_rotation - note_rotation;
    frame.speed_rotation = -line_rotation + (final_flow < 0.0 ? 180.0 : 0.0);
    frame.scale = line_size * note_size;
    frame.alpha = alpha;
    frame.color = value.color(note.group, time);
    if (note.hold() && time > note.end) {
      frame.alpha *= 1.0 - std::clamp((time - note.end) / 0.2, 0.0, 1.0);
    }
    return frame;
  }

  static bool visible(const Vec2& point, double radius, float width, float height) {
    return point.x + radius >= 0.0 && point.x - radius <= width &&
           point.y + radius >= 0.0 && point.y - radius <= height;
  }

  void addText(Vec2 position, double size, std::string_view text, uint32_t rgba,
               double anchor_x = 0.5, double anchor_y = 0.5,
               double rotation = 0.0, double scale_x = 1.0, double scale_y = 1.0) {
    commands.addString(
        PLUVIORA_COMMAND_TEXT, 0,
        TextPayload{static_cast<float>(position.x), static_cast<float>(position.y),
                    static_cast<float>(size), static_cast<float>(rotation),
                    static_cast<float>(scale_x), static_cast<float>(scale_y),
                    static_cast<float>(anchor_x), static_cast<float>(anchor_y), rgba,
                    static_cast<uint32_t>(text.size())},
        text);
  }

  static uint32_t storyboardBuiltin(std::string_view name) {
    if (name == "builtin.line" || name == "builtin.line_pixel") return PLUVIORA_STORYBOARD_LINE;
    if (name == "builtin.rect" || name == "builtin.rectangle") return PLUVIORA_STORYBOARD_RECT;
    if (name == "builtin.round_rect") return PLUVIORA_STORYBOARD_ROUND_RECT;
    if (name == "builtin.line_head") return PLUVIORA_STORYBOARD_LINE_HEAD;
    if (name == "builtin.line_vertical") return PLUVIORA_STORYBOARD_LINE_VERTICAL;
    if (name == "builtin.tap" || name == "builtin.click") return PLUVIORA_STORYBOARD_TAP;
    if (name == "builtin.hold") return PLUVIORA_STORYBOARD_HOLD;
    if (name == "builtin.drag") return PLUVIORA_STORYBOARD_DRAG;
    if (name == "builtin.fracture" || name == "builtin.extap") return PLUVIORA_STORYBOARD_FRACTURE;
    if (name == "builtin.tap_double") return PLUVIORA_STORYBOARD_TAP_DOUBLE;
    if (name == "builtin.extap_double") return PLUVIORA_STORYBOARD_FRACTURE_DOUBLE;
    if (name == "builtin.exhold") return PLUVIORA_STORYBOARD_EXHOLD;
    return PLUVIORA_STORYBOARD_CUSTOM;
  }

  void addStoryboards(StoryboardLayer layer, double time, float width, float height,
                      double line_head_base) {
    Chart& value = *chart;
    for (Storyboard& storyboard : value.storyboards) {
      if (storyboard.layer != layer) continue;
      const Vec2 position = value.position(storyboard.group, time, width, height);
      const double alpha = value.get(storyboard.group, 2, time);
      const double size = value.get(storyboard.group, 3, time);
      const double rotation = -value.get(storyboard.group, 4, time);
      const double sb_width = value.get(storyboard.group, 10, time);
      const double sb_height = value.get(storyboard.group, 11, time);
      const Color color = value.color(storyboard.group, time);
      if (alpha * color.a <= 0.0) continue;
      if (storyboard.type == StoryboardType::text) {
        addText(position, (width + height) * 0.025 * size, storyboard.data,
                color.rgba(alpha), 0.5, 0.5, rotation, sb_width, sb_height);
        continue;
      }
      const uint32_t builtin = storyboardBuiltin(storyboard.data);
      double base_width = line_head_base * 2.0;
      double base_height = line_head_base * 2.0;
      if (builtin == PLUVIORA_STORYBOARD_CUSTOM) {
        const auto asset = storyboard_asset_sizes.find(storyboard.data);
        if (asset != storyboard_asset_sizes.end()) {
          base_width = asset->second.x * width / kWorldWidth;
          base_height = asset->second.y * height / kWorldHeight;
        }
      } else if (builtin == PLUVIORA_STORYBOARD_LINE ||
                 builtin == PLUVIORA_STORYBOARD_LINE_VERTICAL) {
        base_width = width / kWorldWidth * 100.0;
        base_height = height / kWorldHeight * 8.0;
      } else if (builtin == PLUVIORA_STORYBOARD_RECT ||
                 builtin == PLUVIORA_STORYBOARD_ROUND_RECT) {
        base_width = width / kWorldWidth * 100.0;
        base_height = height / kWorldHeight * 100.0;
      }
      const double scaled_width = base_width * size * sb_width;
      const double scaled_height = base_height * size * sb_height;
      std::array<Vec2, 4> corners{
          Vec2{value.get(storyboard.group, 18, time) * scaled_width,
               -value.get(storyboard.group, 19, time) * scaled_height},
          Vec2{value.get(storyboard.group, 20, time) * scaled_width,
               -value.get(storyboard.group, 21, time) * scaled_height},
          Vec2{value.get(storyboard.group, 16, time) * scaled_width,
               -value.get(storyboard.group, 17, time) * scaled_height},
          Vec2{value.get(storyboard.group, 14, time) * scaled_width,
               -value.get(storyboard.group, 15, time) * scaled_height},
      };
      for (Vec2& corner : corners) corner = position + rotate(corner, rotation);
      const StoryboardImagePayload payload{
          static_cast<float>(corners[0].x), static_cast<float>(corners[0].y),
          static_cast<float>(corners[1].x), static_cast<float>(corners[1].y),
          static_cast<float>(corners[2].x), static_cast<float>(corners[2].y),
          static_cast<float>(corners[3].x), static_cast<float>(corners[3].y),
          color.rgba(alpha), builtin, static_cast<uint32_t>(storyboard.data.size())};
      commands.addString(PLUVIORA_COMMAND_STORYBOARD_IMAGE, 0, payload, storyboard.data);
    }
  }

  void addHitEffects(double time, float width, float height, double line_head_base,
                     bool add_rings, bool add_particles) {
    Chart& value = *chart;
    for (size_t line_index = 0; line_index < value.lines.size(); ++line_index) {
      Line& line = value.lines[line_index];
      for (size_t note_index = 0; note_index < line.notes.size(); ++note_index) {
        Note& note = line.notes[note_index];
        if (note.fake || note.start > time || note.end + kHitDuration < time) continue;
        NoteFrame frame = noteFrame(line, note, time, width, height);
        const uint64_t base_seed = value.content_hash ^ (line_index << 32) ^ note.logical_index;
        const double ring_progress = (time - note.start) / kHitDuration;
        if (add_rings && ring_progress >= 0.0 && ring_progress <= 1.0) {
          const double ring_size = line_head_base * 4.632 *
                                   (1.0 - std::pow(1.0 - ring_progress, 3.0)) * frame.scale;
          commands.add(PLUVIORA_COMMAND_HIT_RING, 0,
                       HitRingPayload{static_cast<float>(frame.head.x),
                                      static_cast<float>(frame.head.y),
                                      static_cast<float>(ring_size),
                                      static_cast<float>(ring_progress),
                                      static_cast<float>(randomRange(base_seed, 0.0, 360.0)),
                                      Color{0.56 + ring_progress * 0.08,
                                            0.77 - ring_progress * 0.36,
                                            0.99, 1.0}.rgba()});
        }
        if (!add_particles) continue;
        int first_spawn = 0;
        int last_spawn = note.hold()
                             ? static_cast<int>(std::floor((std::min(time, note.end) - note.start) / 0.01))
                             : 9;
        if (note.hold()) {
          first_spawn = std::max(0, static_cast<int>(std::ceil(
                                        (time - kHitDuration - note.start) / 0.01)));
        }
        for (int spawn = first_spawn; spawn <= last_spawn; ++spawn) {
          const double particle_time = note.hold() ? note.start + spawn * 0.01 : note.start;
          const double progress = (time - particle_time) / kHitDuration;
          if (progress < 0.0 || progress > 1.0) continue;
          const uint64_t seed = mix64(base_seed ^ static_cast<uint64_t>(spawn));
          const double angle = randomRange(seed, 0.0, 360.0);
          const double speed = randomRange(seed + 1, 0.3, 0.72);
          const double initial_size = std::pow(speed, 0.22) * randomRange(seed + 2, 0.6, 0.7) / 42.0;
          const double scale_x = randomRange(seed + 3, 1.5, 2.1);
          const double scale_y = randomRange(seed + 4, -0.5, 0.5);
          const double gravity = randomRange(seed + 5, 0.9, 1.3);
          const double radius = progress * speed * speed *
                                (progress * progress / 3.0 - progress + 1.0) *
                                (width + height) * frame.scale * value.note_scale;
          Vec2 offset = rotate({radius, 0.0}, angle);
          offset.y += progress * progress * gravity * 0.025 *
                      (width + height) * frame.scale * value.note_scale;
          const Vec2 position = frame.head + offset;
          const double base_size = initial_size * (width + height) * frame.scale * value.note_scale;
          const double radius_x = std::pow(progress + 1.0, -scale_x) * 1.34 /
                                  (progress + 1.0) * base_size;
          const double radius_y = std::pow(progress + 1.0, -scale_y) * 0.25 /
                                  (progress + 1.0) * base_size;
          if (!visible(position, std::max(radius_x, radius_y), width, height)) continue;
          const double alpha = std::clamp(std::min(progress / 0.128,
                                                   (1.0 - progress) / (1.0 - 0.805)),
                                          0.0, 1.0);
          commands.add(PLUVIORA_COMMAND_PARTICLE, 0,
                       ParticlePayload{static_cast<float>(position.x),
                                       static_cast<float>(position.y),
                                       static_cast<float>(radius_x),
                                       static_cast<float>(radius_y),
                                       static_cast<float>(angle),
                                       Color{0.56 + progress * 0.08,
                                             0.77 - progress * 0.36,
                                             0.99, alpha}.rgba()});
        }
      }
    }
  }

  PluvioraStatus render(double time, float width, float height, double song_length,
                        PluvioraFrameView* output) {
    if (!chart) return PLUVIORA_NOT_LOADED;
    if (!output || !std::isfinite(time) || width <= 0.0f || height <= 0.0f) {
      return PLUVIORA_INVALID_ARGUMENT;
    }
    Chart& value = *chart;
    commands.clear();
    uint32_t hit_count = 0;
    uint32_t drag_count = 0;
    uint32_t fracture_count = 0;
    if (std::isfinite(last_render_time) && time >= last_render_time) {
      for (const Line& line : value.lines) {
        for (const Note& note : line.notes) {
          if (note.fake || note.start <= last_render_time || note.start > time) continue;
          if (note.type == NoteType::hit) ++hit_count;
          else if (note.type == NoteType::drag) ++drag_count;
          else ++fracture_count;
        }
      }
    }
    last_render_time = time;

    const double line_head_base = (width + height) * 0.0223 * value.note_scale;
    addStoryboards(StoryboardLayer::background, time, width, height, line_head_base);
    commands.add(PLUVIORA_COMMAND_RECT, 0,
                 RectPayload{width * 0.5f, height * 0.5f, width, height, 0.0f,
                             Color{0.0, 0.0, 0.0, 0.8}.rgba()});
    addStoryboards(StoryboardLayer::normal, time, width, height, line_head_base);

    for (Line& line : value.lines) {
      const Vec2 position = value.position(line.group, time, width, height);
      const double rotation = value.get(line.group, 4, time);
      const double alpha = value.get(line.group, 2, time);
      const double head_alpha = value.get(line.group, 9, time) * alpha;
      const double body_alpha = value.get(line.group, 8, time) * alpha;
      const double line_size = value.get(line.group, 3, time);
      const Color line_color = value.color(line.group, time);
      if (head_alpha > 0.0 && visible(position, line_head_base * line_size, width, height)) {
        commands.add(PLUVIORA_COMMAND_SPRITE, 0,
                     SpritePayload{static_cast<float>(position.x), static_cast<float>(position.y),
                                   static_cast<float>(line_head_base * line_size),
                                   static_cast<float>(line_head_base * line_size),
                                   static_cast<float>(180.0 - rotation),
                                   line_color.rgba(head_alpha), PLUVIORA_SPRITE_LINE_HEAD});
      }
      if (body_alpha > 0.0) {
        const double connect = line_head_base * 0.446524 * line_size;
        const double length = height * 2.5 * line_size;
        const double half_width = line_head_base * 0.096774 * line_size * 0.5;
        const Vec2 axis = rotate({1.0, 0.0}, -rotation);
        const Vec2 normal{-axis.y, axis.x};
        const Vec2 start = position + axis * connect;
        const Vec2 end = start + axis * length;
        const std::array<Vec2, 4> points{start - normal * half_width,
                                         end - normal * half_width,
                                         end + normal * half_width,
                                         start + normal * half_width};
        commands.add(PLUVIORA_COMMAND_QUAD, 0,
                     QuadPayload{static_cast<float>(points[0].x), static_cast<float>(points[0].y),
                                 static_cast<float>(points[1].x), static_cast<float>(points[1].y),
                                 static_cast<float>(points[2].x), static_cast<float>(points[2].y),
                                 static_cast<float>(points[3].x), static_cast<float>(points[3].y),
                                 line_color.rgba(body_alpha)});
      }

    }

    addHitEffects(time, width, height, line_head_base, true, false);

    for (Line& line : value.lines) {
      for (Note& note : line.notes) {
        if (note.end + 0.2 < time) continue;
        NoteFrame frame = noteFrame(line, note, time, width, height);
        const double note_width = line_head_base * 1.77 * frame.scale;
        const double head = line_head_base * 0.885 * frame.scale;
        const double body = std::min(8192.0, (frame.head - frame.tail).length());
        const double radius = std::max(note_width, head + body) * 0.75;
        if (frame.alpha <= 0.0 || !visible(frame.head, radius, width, height)) continue;
        uint32_t kind = PLUVIORA_NOTE_TAP;
        if (note.hold()) kind = PLUVIORA_NOTE_HOLD;
        else if (note.type == NoteType::drag) kind = PLUVIORA_NOTE_DRAG;
        else if (note.type == NoteType::fracture) kind = PLUVIORA_NOTE_FRACTURE;
        uint32_t note_flags = 0;
        if (note.simultaneous) note_flags |= 1u;
        if (note.always_perfect) note_flags |= 2u;
        if (note.fake) note_flags |= 4u;
        commands.add(PLUVIORA_COMMAND_NOTE, 0,
                     NotePayload{static_cast<float>(frame.head.x),
                                 static_cast<float>(frame.head.y),
                                 static_cast<float>(frame.rotation),
                                 static_cast<float>(note_width),
                                 static_cast<float>(head),
                                 static_cast<float>(body),
                                 static_cast<float>(head),
                                 frame.color.rgba(frame.alpha), kind, note_flags});
      }
    }

    addHitEffects(time, width, height, line_head_base, false, true);
    addStoryboards(StoryboardLayer::foreground, time, width, height, line_head_base);

    const double safe_song_length = song_length > 0.0 ? song_length : std::max(value.duration, 1.0);
    commands.add(PLUVIORA_COMMAND_RECT, 0,
                 RectPayload{static_cast<float>(std::clamp(time / safe_song_length, 0.0, 1.0) * width * 0.5),
                             width * 0.00234375f,
                             static_cast<float>(std::clamp(time / safe_song_length, 0.0, 1.0) * width),
                             width * 0.0046875f, 0.0f, 0xffffffffu});
    commands.add(PLUVIORA_COMMAND_SPRITE, 0,
                 SpritePayload{width * 0.0494792f, width * 0.0489583f,
                               width * 0.040625f, width * 0.040625f, 0.0f,
                               0xffffffabu, PLUVIORA_SPRITE_PAUSE});
    addText({width * 0.0994791, width * 0.0397208}, width * 0.0201352,
            value.meta.title, 0xffffffffu, 0.0, 0.5);
    std::string difficulty = value.meta.difficulty + " " +
                             std::to_string(static_cast<int>(value.meta.difficulty_value));
    if (std::fmod(value.meta.difficulty_value, 1.0) != 0.0) difficulty += "+";
    addText({width * 0.0994791, width * 0.0604583}, width * 0.0151472,
            difficulty, 0xffffffbfu, 0.0, 0.5);
    const uint32_t combo = static_cast<uint32_t>(std::upper_bound(
                               value.combo_times.begin(), value.combo_times.end(), time) -
                           value.combo_times.begin());
    const uint32_t score = value.combo_times.empty()
                               ? 1010000u
                               : static_cast<uint32_t>(std::clamp(
                                     std::ceil(1010000.0 / value.combo_times.size() * combo),
                                     0.0, 1010000.0));
    std::string score_text = std::to_string(score);
    if (score_text.size() < 7) score_text.insert(0, 7 - score_text.size(), '0');
    addText({width * 0.9752375, width * 0.0395833}, width * 0.0268352,
            score_text, 0xffffffffu, 1.0, 0.5);
    addText({width * 0.9752375, width * 0.06684375}, width * 0.0201352,
            "100.00%", 0xffffffbfu, 1.0, 0.5);
    addText({width * 0.5, width * 0.0359375}, width * 0.0201352,
            "ALL PERFECT", 0xffffffffu);
    addText({width * 0.5, width * 0.0677083}, width * 0.0263352,
            std::to_string(combo), 0xffffffffu);

    *output = PluvioraFrameView{commands.data(), commands.size(), commands.capacity(),
                                commands.count(), hit_count, drag_count,
                                fracture_count, 0};
    return PLUVIORA_OK;
  }
};

template <typename Callback>
PluvioraStatus protect(Engine* engine, Callback&& callback) {
  if (!engine) return PLUVIORA_INVALID_HANDLE;
  try {
    engine->last_error.clear();
    return callback();
  } catch (const std::exception& error) {
    engine->last_error = error.what();
    return PLUVIORA_CHART_ERROR;
  } catch (...) {
    engine->last_error = "unknown native error";
    return PLUVIORA_INTERNAL_ERROR;
  }
}

}  // namespace pluviora

extern "C" {

uint32_t pluviora_abi_version(void) { return PLUVIORA_ABI_VERSION; }

PluvioraHandle pluviora_create(void) {
  try {
    return new pluviora::Engine();
  } catch (...) {
    return nullptr;
  }
}

PluvioraStatus pluviora_destroy(PluvioraHandle handle) {
  if (!handle) return PLUVIORA_OK;
  delete static_cast<pluviora::Engine*>(handle);
  return PLUVIORA_OK;
}

PluvioraStatus pluviora_load(PluvioraHandle handle, const uint8_t* json_data,
                              size_t json_length, const uint8_t* js_data,
                              size_t js_length) {
  auto* engine = static_cast<pluviora::Engine*>(handle);
  if (!engine) return PLUVIORA_INVALID_HANDLE;
  if (!json_data || json_length == 0 || (!js_data && js_length != 0)) {
    return PLUVIORA_INVALID_ARGUMENT;
  }
  return pluviora::protect(engine, [&] {
    engine->warnings.clear();
    const std::string_view json(reinterpret_cast<const char*>(json_data), json_length);
    const std::string_view js(js_data ? reinterpret_cast<const char*>(js_data) : "", js_length);
    pluviora::Chart parsed = pluviora::parseChart(json, js, engine->warnings);
    engine->chart = std::move(parsed);
    engine->storyboard_asset_sizes.clear();
    engine->last_render_time = std::numeric_limits<double>::quiet_NaN();
    return PLUVIORA_OK;
  });
}

PluvioraStatus pluviora_render(PluvioraHandle handle, double time_seconds,
                                float width, float height, double song_length,
                                PluvioraFrameView* out_frame) {
  auto* engine = static_cast<pluviora::Engine*>(handle);
  return pluviora::protect(engine, [&] {
    return engine->render(time_seconds, width, height, song_length, out_frame);
  });
}

PluvioraStatus pluviora_get_metadata(PluvioraHandle handle,
                                      PluvioraMetadataView* out_metadata) {
  auto* engine = static_cast<pluviora::Engine*>(handle);
  if (!engine) return PLUVIORA_INVALID_HANDLE;
  if (!out_metadata) return PLUVIORA_INVALID_ARGUMENT;
  if (!engine->chart) return PLUVIORA_NOT_LOADED;
  const pluviora::Chart& chart = *engine->chart;
  *out_metadata = PluvioraMetadataView{
      chart.meta.title.c_str(), chart.meta.composer.c_str(), chart.meta.illustrator.c_str(),
      chart.meta.beatmapper.c_str(), chart.meta.difficulty.c_str(),
      chart.meta.audio_file.c_str(), chart.meta.illustration_file.c_str(),
      chart.meta.difficulty_value, chart.duration,
      static_cast<uint32_t>(chart.lines.size()), chart.note_count,
      chart.animation_count, static_cast<uint32_t>(chart.storyboards.size()),
      chart.content_hash};
  return PLUVIORA_OK;
}

PluvioraStatus pluviora_set_note_scale(PluvioraHandle handle, double scale) {
  auto* engine = static_cast<pluviora::Engine*>(handle);
  if (!engine) return PLUVIORA_INVALID_HANDLE;
  if (!engine->chart) return PLUVIORA_NOT_LOADED;
  if (!std::isfinite(scale) || scale <= 0.0) return PLUVIORA_INVALID_ARGUMENT;
  engine->chart->note_scale = scale;
  return PLUVIORA_OK;
}

PluvioraStatus pluviora_set_flow_speed(PluvioraHandle handle, double speed) {
  auto* engine = static_cast<pluviora::Engine*>(handle);
  if (!engine) return PLUVIORA_INVALID_HANDLE;
  if (!engine->chart) return PLUVIORA_NOT_LOADED;
  if (!std::isfinite(speed) || speed <= 0.0) return PLUVIORA_INVALID_ARGUMENT;
  engine->chart->flow_speed = speed;
  return PLUVIORA_OK;
}

PluvioraStatus pluviora_set_storyboard_asset_size(
    PluvioraHandle handle, const char* name, float width, float height) {
  auto* engine = static_cast<pluviora::Engine*>(handle);
  if (!engine) return PLUVIORA_INVALID_HANDLE;
  if (!engine->chart) return PLUVIORA_NOT_LOADED;
  if (!name || !std::isfinite(width) || !std::isfinite(height) ||
      width <= 0.0f || height <= 0.0f) {
    return PLUVIORA_INVALID_ARGUMENT;
  }
  engine->storyboard_asset_sizes[std::string(name)] = {width, height};
  return PLUVIORA_OK;
}

uint32_t pluviora_warning_count(PluvioraHandle handle) {
  auto* engine = static_cast<pluviora::Engine*>(handle);
  return engine ? static_cast<uint32_t>(engine->warnings.size()) : 0;
}

const char* pluviora_warning_at(PluvioraHandle handle, uint32_t index) {
  auto* engine = static_cast<pluviora::Engine*>(handle);
  if (!engine || index >= engine->warnings.size()) return "";
  return engine->warnings[index].c_str();
}

const char* pluviora_last_error(PluvioraHandle handle) {
  auto* engine = static_cast<pluviora::Engine*>(handle);
  return engine ? engine->last_error.c_str() : "invalid handle";
}

}  // extern "C"
