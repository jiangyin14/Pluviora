#include "pluviora.h"

#include "compat/source_easing_tables.hpp"
#include "third_party/yyjson/yyjson.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb/stb_truetype.h"

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
#include <random>
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
  Vec2& operator-=(const Vec2& other) {
    x -= other.x;
    y -= other.y;
    return *this;
  }
  double length() const { return std::hypot(x, y); }
  double max() const { return std::max(x, y); }
  double atanDegrees() const { return std::atan2(y, x) / kPi * 180.0; }
};

struct Rect {
  double x = 0.0;
  double y = 0.0;
  double width = 0.0;
  double height = 0.0;

  Vec2 center() const { return {x + width / 2.0, y + height / 2.0}; }
  Rect extend(double padding) const {
    return {x - padding, y - padding, width + padding * 2.0,
            height + padding * 2.0};
  }
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

std::array<Vec2, 4> makeQuad(Vec2 position, Vec2 size,
                             double rotation_degrees) {
  const double radians = rotation_degrees / 180.0 * kPi;
  const double sine = std::sin(radians);
  const double cosine = std::cos(radians);
  const Vec2 half = size / 2.0;
  return {
      position + Vec2{half.x * cosine - half.y * sine,
                      half.x * sine + half.y * cosine},
      position + Vec2{half.x * cosine + half.y * sine,
                      half.x * sine - half.y * cosine},
      position + Vec2{-half.x * cosine + half.y * sine,
                      -half.x * sine - half.y * cosine},
      position + Vec2{-half.x * cosine - half.y * sine,
                      -half.x * sine + half.y * cosine},
  };
}

bool pointStrictlyInConvexQuad(const Vec2& point,
                               const std::array<Vec2, 4>& quad) {
  auto cross = [](double ax, double ay, double bx, double by) {
    return ax * by - ay * bx;
  };
  const double cross0 = cross(quad[1].x - quad[0].x,
                              quad[1].y - quad[0].y,
                              point.x - quad[0].x,
                              point.y - quad[0].y);
  const double cross1 = cross(quad[2].x - quad[1].x,
                              quad[2].y - quad[1].y,
                              point.x - quad[1].x,
                              point.y - quad[1].y);
  const double cross2 = cross(quad[3].x - quad[2].x,
                              quad[3].y - quad[2].y,
                              point.x - quad[2].x,
                              point.y - quad[2].y);
  const double cross3 = cross(quad[0].x - quad[3].x,
                              quad[0].y - quad[3].y,
                              point.x - quad[3].x,
                              point.y - quad[3].y);
  return (cross0 < 0.0 && cross1 < 0.0 && cross2 < 0.0 && cross3 < 0.0) ||
         (cross0 > 0.0 && cross1 > 0.0 && cross2 > 0.0 && cross3 > 0.0);
}

bool pointStrictlyInRect(const Vec2& point, const Rect& rect) {
  return rect.x < point.x && point.x < rect.x + rect.width &&
         rect.y < point.y && point.y < rect.y + rect.height;
}

bool lineSegmentIntersectsLineSegment(const std::array<Vec2, 2>& first,
                                      const std::array<Vec2, 2>& second) {
  constexpr double epsilon = 1e-9;
  const Vec2 p1 = first[0];
  const Vec2 p2 = first[1];
  const Vec2 q1 = second[0];
  const Vec2 q2 = second[1];
  const Vec2 r = p2 - p1;
  const Vec2 s = q2 - q1;
  const Vec2 qp = q1 - p1;
  const double rxs = r.x * s.y - r.y * s.x;
  const double qpxr = qp.x * r.y - qp.y * r.x;
  if (std::abs(rxs) < epsilon) {
    if (std::abs(qpxr) >= epsilon) return false;
    const Vec2 axis = std::abs(r.x) > std::abs(r.y) ? Vec2{1.0, 0.0}
                                                    : Vec2{0.0, 1.0};
    auto project = [&](const Vec2& a, const Vec2& b) {
      const double first_projection = a.x * axis.x + a.y * axis.y;
      const double second_projection = b.x * axis.x + b.y * axis.y;
      return std::pair{std::min(first_projection, second_projection),
                       std::max(first_projection, second_projection)};
    };
    const auto first_projection = project(p1, p2);
    const auto second_projection = project(q1, q2);
    return first_projection.second + epsilon >= second_projection.first &&
           second_projection.second + epsilon >= first_projection.first;
  }
  const double u = (qp.x * s.y - qp.y * s.x) / rxs;
  const double v = (qp.x * r.y - qp.y * r.x) / rxs;
  return u >= -epsilon && u <= 1.0 + epsilon && v >= -epsilon &&
         v <= 1.0 + epsilon;
}

bool quadStrictlyIntersectsRect(const std::array<Vec2, 4>& quad,
                                const Rect& rect) {
  const std::array<std::array<Vec2, 2>, 4> quad_lines{{
      {quad[0], quad[1]},
      {quad[1], quad[2]},
      {quad[2], quad[3]},
      {quad[3], quad[0]},
  }};
  const std::array<Vec2, 4> rect_points{
      Vec2{rect.x, rect.y},
      Vec2{rect.x + rect.width, rect.y},
      Vec2{rect.x + rect.width, rect.y + rect.height},
      Vec2{rect.x, rect.y + rect.height},
  };
  const std::array<std::array<Vec2, 2>, 4> rect_lines{{
      {rect_points[0], rect_points[1]},
      {rect_points[1], rect_points[2]},
      {rect_points[2], rect_points[3]},
      {rect_points[3], rect_points[0]},
  }};
  for (const Vec2& point : quad) {
    if (pointStrictlyInRect(point, rect)) return true;
  }
  for (const Vec2& point : rect_points) {
    if (pointStrictlyInConvexQuad(point, quad)) return true;
  }
  for (const auto& quad_line : quad_lines) {
    for (const auto& rect_line : rect_lines) {
      if (lineSegmentIntersectsLineSegment(quad_line, rect_line)) return true;
    }
  }
  return false;
}

bool lineIntersectsLineSegment(const Vec2& line_point, double line_degrees,
                               const std::array<Vec2, 2>& segment) {
  const double angle = line_degrees / 180.0 * kPi;
  const Vec2 direction{std::cos(angle), std::sin(angle)};
  const Vec2 segment_direction = segment[1] - segment[0];
  const Vec2 q = segment[0] - line_point;
  const double rxs = direction.x * segment_direction.y -
                     direction.y * segment_direction.x;
  const double qxs = q.x * segment_direction.y - q.y * segment_direction.x;
  constexpr double epsilon = 1e-9;
  if (std::abs(rxs) < epsilon) return std::abs(qxs) < epsilon;
  const double u = (q.x * direction.y - q.y * direction.x) / rxs;
  return u >= -epsilon && u <= 1.0 + epsilon;
}

bool lineIntersectsRect(const Vec2& line_point, double line_degrees,
                        const Rect& rect) {
  const std::array<Vec2, 4> points{
      Vec2{rect.x, rect.y},
      Vec2{rect.x + rect.width, rect.y},
      Vec2{rect.x + rect.width, rect.y + rect.height},
      Vec2{rect.x, rect.y + rect.height},
  };
  return lineIntersectsLineSegment(line_point, line_degrees,
                                   {points[0], points[1]}) ||
         lineIntersectsLineSegment(line_point, line_degrees,
                                   {points[1], points[2]}) ||
         lineIntersectsLineSegment(line_point, line_degrees,
                                   {points[2], points[3]}) ||
         lineIntersectsLineSegment(line_point, line_degrees,
                                   {points[3], points[0]});
}

bool lineIsLeavingScreen(const Vec2& line_point, double line_degrees,
                         const Rect& screen) {
  const double radians = (line_degrees + 90.0) / 180.0 * kPi;
  const Vec2 direction{std::cos(radians), std::sin(radians)};
  const Vec2 to_center = screen.center() - line_point;
  const bool point_is_leaving =
      to_center.x * direction.x + to_center.y * direction.y > 0.0;
  return !lineIntersectsRect(line_point, line_degrees, screen) &&
         point_is_leaving;
}

uint64_t fnv1a(std::string_view bytes) {
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}

double easingIn(uint32_t press, double p) {
  switch (press) {
    case 0: return p;
    case 1: return 1.0 - std::cos(p * kPi / 2.0);
    case 2: return std::pow(p, 2.0);
    case 3: return std::pow(p, 3.0);
    case 4: return std::pow(p, 4.0);
    case 5: return std::pow(p, 5.0);
    case 6: return p == 0.0 ? 0.0 : std::pow(2.0, 10.0 * p - 10.0);
    case 7: return 1.0 - std::pow(1.0 - std::pow(p, 2.0), 0.5);
    case 8: return 2.70158 * std::pow(p, 3.0) - 1.70158 * std::pow(p, 2.0);
    case 9:
      return p == 0.0
                 ? 0.0
                 : (p == 1.0
                        ? 1.0
                        : -std::pow(2.0, 10.0 * p - 10.0) *
                              std::sin((p * 10.0 - 10.75) * (2.0 * kPi / 3.0)));
    case 10: {
      const double q = 1.0 - p;
      const double bounced =
          q < 1.0 / 2.75
              ? 7.5625 * std::pow(q, 2.0)
              : (q < 2.0 / 2.75
                     ? 7.5625 * std::pow(q - 1.5 / 2.75, 2.0) + 0.75
                     : (q < 2.5 / 2.75
                            ? 7.5625 * std::pow(q - 2.25 / 2.75, 2.0) + 0.9375
                            : 7.5625 * std::pow(q - 2.625 / 2.75, 2.0) +
                                  0.984375));
      return 1.0 - bounced;
    }
    default: return p;
  }
}

double easingOut(uint32_t press, double p) {
  switch (press) {
    case 0: return p;
    case 1: return std::sin(p * kPi / 2.0);
    case 2: return 1.0 - std::pow(1.0 - p, 2.0);
    case 3: return 1.0 - std::pow(1.0 - p, 3.0);
    case 4: return 1.0 - std::pow(1.0 - p, 4.0);
    case 5: return 1.0 - std::pow(1.0 - p, 5.0);
    case 6: return p == 1.0 ? 1.0 : 1.0 - std::pow(2.0, -10.0 * p);
    case 7: return std::pow(1.0 - std::pow(p - 1.0, 2.0), 0.5);
    case 8:
      return 1.0 + 2.70158 * std::pow(p - 1.0, 3.0) +
             1.70158 * std::pow(p - 1.0, 2.0);
    case 9:
      return p == 0.0
                 ? 0.0
                 : (p == 1.0
                        ? 1.0
                        : std::pow(2.0, -10.0 * p) *
                                  std::sin((p * 10.0 - 0.75) *
                                           (2.0 * kPi / 3.0)) +
                              1.0);
    case 10:
      return p < 1.0 / 2.75
                 ? 7.5625 * std::pow(p, 2.0)
                 : (p < 2.0 / 2.75
                        ? 7.5625 * std::pow(p - 1.5 / 2.75, 2.0) + 0.75
                        : (p < 2.5 / 2.75
                               ? 7.5625 * std::pow(p - 2.25 / 2.75, 2.0) +
                                     0.9375
                               : 7.5625 * std::pow(p - 2.625 / 2.75, 2.0) +
                                     0.984375));
    default: return p;
  }
}

double easingInOut(uint32_t press, double p) {
  switch (press) {
    case 0: return p;
    case 1: return -(std::cos(kPi * p) - 1.0) / 2.0;
    case 2:
      return p < 0.5 ? 2.0 * std::pow(p, 2.0)
                     : 1.0 - std::pow(-2.0 * p + 2.0, 2.0) / 2.0;
    case 3:
      return p < 0.5 ? 4.0 * std::pow(p, 3.0)
                     : 1.0 - std::pow(-2.0 * p + 2.0, 3.0) / 2.0;
    case 4:
      return p < 0.5 ? 8.0 * std::pow(p, 4.0)
                     : 1.0 - std::pow(-2.0 * p + 2.0, 4.0) / 2.0;
    case 5:
      return p < 0.5 ? 16.0 * std::pow(p, 5.0)
                     : 1.0 - std::pow(-2.0 * p + 2.0, 5.0) / 2.0;
    case 6:
      return p == 0.0
                 ? 0.0
                 : (p == 1.0
                        ? 1.0
                        : (p < 0.5 ? std::pow(2.0, 20.0 * p - 10.0)
                                   : 2.0 - std::pow(2.0, -20.0 * p + 10.0)) /
                              2.0);
    case 7:
      return p < 0.5
                 ? (1.0 - std::pow(1.0 - std::pow(2.0 * p, 2.0), 0.5)) /
                       2.0
                 : (std::pow(1.0 - std::pow(-2.0 * p + 2.0, 2.0), 0.5) +
                    1.0) /
                       2.0;
    case 8:
      return p < 0.5
                 ? (std::pow(2.0 * p, 2.0) *
                    (((2.5949095 + 1.0) * 2.0 * p) - 2.5949095)) /
                       2.0
                 : (std::pow(2.0 * p - 2.0, 2.0) *
                            ((2.5949095 + 1.0) * (p * 2.0 - 2.0) +
                             2.5949095) +
                        2.0) /
                       2.0;
    case 9:
      return p == 0.0
                 ? 0.0
                 : (p == 0.0
                        ? 1.0
                        : (p < 0.5
                               ? -std::pow(2.0, 20.0 * p - 10.0) *
                                     std::sin((20.0 * p - 11.125) *
                                              (2.0 * kPi / 4.5)) /
                                     2.0
                               : std::pow(2.0, -20.0 * p + 10.0) *
                                         std::sin((20.0 * p - 11.125) *
                                                  (2.0 * kPi / 4.5)) /
                                         2.0 +
                                     1.0));
    case 10:
      return p < 0.5 ? (1.0 - easingOut(10, 1.0 - 2.0 * p)) / 2.0
                     : (1.0 + easingOut(10, 2.0 * p - 1.0)) / 2.0;
    default: return p;
  }
}

double easing(uint32_t press, uint32_t direction, double p) {
  switch (std::clamp(direction, 0u, 2u)) {
    case 0: return easingIn(std::clamp(press, 0u, 10u), p);
    case 1: return easingOut(std::clamp(press, 0u, 10u), p);
    case 2: return easingInOut(std::clamp(press, 0u, 10u), p);
  }
  return p;
}

double easingIntegral(uint32_t direction, double p) {
  direction = std::clamp(direction, 0u, 2u);
  p = std::clamp(p, 0.0, 1.0);
  const auto& table = compat::kSourceIntegralTables[direction];
  if (p == 1.0) return table[127];
  const double cursor = p * 127.0;
  const auto index = static_cast<size_t>(cursor);
  const double local = std::fmod(p, 1.0 / 127.0) * 127.0;
  return table[index] + (table[index + 1] - table[index]) * local;
}

enum class ObjectType : uint8_t { line = 0, note = 1, storyboard = 2 };
enum class NoteType : uint8_t { hit = 0, drag = 1 };
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
      case 23: return 2500.0 / 1080.0;
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
    if (function != 0) p = easing(function, direction, p);
    if (!samples.empty()) {
      if (samples.size() == 1) return samples.front();
      p = std::clamp(p, 0.0, 1.0);
      const double intervals = static_cast<double>(samples.size() - 1);
      const size_t index = std::min(static_cast<size_t>(p * intervals),
                                    samples.size() - 1);
      const size_t next = std::min(index + 1, samples.size() - 1);
      p = std::fmod(p, 1.0 / intervals) * intervals;
      return samples[index] + (samples[next] - samples[index]) * p;
    }
    return from + (to - from) * p;
  }

  double integral(double time) const {
    const double p = progress(time);
    const double integral_progress =
        function == 0 ? p * p / 2.0 : easingIntegral(direction, p);
    double result =
        (end - start) * (from * p + (to - from) * integral_progress);
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

  std::optional<double> alwaysValue(ObjectType object, uint32_t key) const {
    if (events.empty()) return defaultValue(object, key);
    if (key == 12) {
      if (events.size() == 1 && events[0].from == events[0].to) {
        return events[0].from;
      }
      return std::nullopt;
    }
    const double fixed_value = events[0].from;
    for (const Event& event : events) {
      if (event.start == event.end) {
        if (event.to != fixed_value) return std::nullopt;
        continue;
      }
      if (event.from != event.to || fixed_value != event.from) {
        return std::nullopt;
      }
    }
    return fixed_value;
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

  std::optional<uint64_t> noteAnimationHash() const {
    constexpr std::array<uint32_t, 8> keys{0, 1, 3, 4, 5, 6, 7, 12};
    uint64_t hash = 0xcbf29ce484222325ull;
    for (uint32_t key : keys) {
      const std::optional<double> value = tracks[key].alwaysValue(type, key);
      if (!value) return std::nullopt;
      double normalized = *value;
      if (normalized == 0.0) normalized = std::copysign(0.0, 1.0);
      const auto* bytes = reinterpret_cast<const uint8_t*>(&normalized);
      for (size_t index = 0; index < sizeof(normalized); ++index) {
        hash ^= bytes[index];
        hash *= 0x100000001b3ull;
      }
    }
    return hash;
  }
};

struct TimeBasedAnimation {
  double duration = 0.15;
  double last_time = 0.0;
  Vec2 value;
  bool combo_easing = false;

  double get(double time) const {
    const double progress =
        std::clamp((time - last_time) / duration, 0.0, 1.0);
    if (combo_easing) return 1.0 + 0.07 * std::sin(progress * kPi);
    return value.x + (value.y - value.x) * progress;
  }

  TimeBasedAnimation& set(double time, double new_value) {
    value = {get(time), new_value};
    last_time = time;
    return *this;
  }

  TimeBasedAnimation& weakSet(double time, double new_value) {
    if (value.y != new_value) set(time, new_value);
    return *this;
  }

  void reset(double new_value = 0.0) { value = {new_value, new_value}; }
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
  bool has_logical_index = false;
  NoteType type = NoteType::hit;
  double start = 0.0;
  double end = 0.0;
  bool fake = false;
  bool always_perfect = false;
  bool simultaneous = false;
  double floor_start = 0.0;
  double floor_end = 0.0;
  uint32_t line = 0;
  double last_update_time = 0.0;
  bool played_hitsound = false;

  bool hold() const { return type == NoteType::hit && start != end; }

  void timeUpdated(double time) {
    if (last_update_time > time) played_hitsound = start < time;
    last_update_time = time;
  }

  bool onPlayHitsound() {
    if (played_hitsound) return false;
    played_hitsound = true;
    return true;
  }
};

struct NoteGroup {
  std::vector<uint32_t> indices;
  bool breakable = true;
  double last_update_time = 0.0;
  uint32_t first_note_index = 0;

  void timeUpdated(double time) {
    if (last_update_time > time) first_note_index = 0;
    last_update_time = time;
  }

  void passedNoteIndex(uint32_t index) {
    if (first_note_index == index) ++first_note_index;
  }
};

struct Line {
  uint32_t group = kInvalidGroup;
  std::vector<Note> notes;
  std::vector<NoteGroup> note_groups;
};

struct Particle {
  double dt = 0.0;
  double rotation = 0.0;
  double initial_speed = 0.0;
  double initial_size = 0.0;
  Vec2 scale;
  double gravity_coefficient = 0.0;

  double radius(double progress) const {
    return progress * initial_speed * initial_speed *
           (progress * progress / 3.0 - progress + 1.0);
  }

  double deltaY(double progress) const {
    return progress * progress * gravity_coefficient * 0.025;
  }

  Vec2 particleScale(double progress) const {
    return {std::pow(progress + 1.0, -scale.x) * 1.34 /
                (progress + 1.0),
            std::pow(progress + 1.0, -scale.y) * 0.25 /
                (progress + 1.0)};
  }
};

struct HitEffect {
  double time = 0.0;
  double texture_rotation = 0.0;
  uint32_t line_index = 0;
  uint32_t note_index = 0;
  std::vector<Particle> particles;

  double endTime(double duration) const {
    return (particles.empty() ? time : time + particles.back().dt) + duration;
  }
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
  std::vector<HitEffect> hit_effects;
  uint64_t content_hash = 0;
  uint32_t animation_count = 0;
  uint32_t note_count = 0;
  double duration = 0.0;
  double note_scale = 1.0;
  double flow_speed = 1.66;
  double last_update_time = 0.0;
  uint32_t first_hit_effect_index = 0;
  TimeBasedAnimation score_animation;
  TimeBasedAnimation combo_scale_animation{.combo_easing = true};

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
    return lerp(colors[first], colors[second], value - from);
  }

  Vec2 position(uint32_t group, double time, float width, float height) {
    const double x = get(group, 0, time) + get(group, 6, time);
    const double y = get(group, 1, time) + get(group, 7, time);
    return {(x + kWorldWidth * 0.5) / kWorldWidth * width,
            (kWorldHeight * 0.5 - y) / kWorldHeight * height};
  }

  void timeUpdated(double time) {
    if (last_update_time > time) {
      first_hit_effect_index = 0;
      score_animation.reset();
    }
    last_update_time = time;
  }

  void seek(double time) {
    last_update_time = time;
    score_animation.reset();
    combo_scale_animation.reset();

    first_hit_effect_index = 0;
    while (first_hit_effect_index < hit_effects.size() &&
           hit_effects[first_hit_effect_index].endTime(kHitDuration) < time) {
      ++first_hit_effect_index;
    }

    for (Line& line : lines) {
      for (Note& note : line.notes) {
        note.last_update_time = time;
        note.played_hitsound = note.start <= time;
      }
      for (NoteGroup& group : line.note_groups) {
        group.last_update_time = time;
        group.first_note_index = 0;
        while (group.first_note_index < group.indices.size()) {
          const Note& note = line.notes[group.indices[group.first_note_index]];
          const bool expired = note.end < time &&
                               (!note.hold() || note.end + 0.2 <= time);
          if (!expired) break;
          ++group.first_note_index;
        }
      }
    }
  }

  void passedHitEffectIndex(uint32_t index) {
    if (first_hit_effect_index == index) ++first_hit_effect_index;
  }
};

yyjson_val* required(yyjson_val* object, const char* key) {
  yyjson_val* value = yyjson_obj_get(object, key);
  if (!value) throw std::runtime_error(std::string("missing ") + key);
  return value;
}

double number(yyjson_val* value, const char* context) {
  if (yyjson_is_num(value)) return yyjson_get_num(value);
  throw std::runtime_error(std::string(context) + " is not numeric");
}

double animationNumber(yyjson_val* value, const char* context) {
  if (yyjson_is_num(value)) return yyjson_get_num(value);
  if (yyjson_is_str(value)) {
    const char* text = yyjson_get_str(value);
    if (!text || *text == '\0') return 0.0;
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
  return {0.0, 0.0, 0.0, 0.0};
}

struct BpmEvent {
  double start = 0.0;
  double bpm = 0.0;
};

uint64_t resolveBpmReference(yyjson_val* value, const char* context,
                             const std::vector<BpmEvent>& bpms,
                             bool& used_value_fallback,
                             bool& used_ambiguous_value_fallback) {
  const double reference = number(value, context);
  if (!std::isfinite(reference) || reference < 0.0) {
    throw std::runtime_error(std::string(context) + " is not a valid BPM reference");
  }
  if (std::floor(reference) == reference &&
      reference < static_cast<double>(bpms.size())) {
    return static_cast<uint64_t>(reference);
  }

  std::optional<uint64_t> matched_index;
  for (size_t index = 0; index < bpms.size(); ++index) {
    const double tolerance =
        std::numeric_limits<double>::epsilon() *
        std::max({1.0, std::abs(reference), std::abs(bpms[index].bpm)}) * 8.0;
    if (std::abs(bpms[index].bpm - reference) > tolerance) continue;
    if (matched_index) used_ambiguous_value_fallback = true;
    else matched_index = static_cast<uint64_t>(index);
  }
  if (!matched_index) {
    throw std::runtime_error(std::string("invalid BPM reference in ") + context);
  }
  used_value_fallback = true;
  return *matched_index;
}

double parseTime(yyjson_val* object, const char* key, uint64_t bpm_index,
                 const std::vector<BpmEvent>& bpms,
                 bool& used_value_fallback,
                 bool& used_ambiguous_value_fallback) {
  yyjson_val* value = required(object, key);
  if (yyjson_is_num(value)) return yyjson_get_num(value);
  if (!yyjson_is_arr(value)) throw std::runtime_error(std::string("invalid ") + key);
  const size_t count = yyjson_arr_size(value);
  if (count != 3 && count != 4) throw std::runtime_error(std::string("invalid ") + key);
  yyjson_val* item0 = yyjson_arr_get(value, 0);
  yyjson_val* item1 = yyjson_arr_get(value, 1);
  yyjson_val* item2 = yyjson_arr_get(value, 2);
  const double division = number(item2, key);
  if (count == 4) {
    bpm_index = resolveBpmReference(yyjson_arr_get(value, 3), key, bpms,
                                    used_value_fallback,
                                    used_ambiguous_value_fallback);
  }
  if (division == 0.0 || bpm_index >= bpms.size() || bpms[bpm_index].bpm == 0.0) {
    throw std::runtime_error(std::string("invalid BPM reference in ") + key);
  }
  const double beats = number(item0, key) + number(item1, key) / division;
  return bpms[bpm_index].start + beats * 60.0 / bpms[bpm_index].bpm;
}

Chart parseChart(std::string_view json, std::string_view ordering_script,
                 std::vector<std::string>& warnings) {
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
  bool used_bpm_value_fallback = false;
  bool used_ambiguous_bpm_value_fallback = false;

  yyjson_val* line_array = required(root, "lines");
  if (!yyjson_is_arr(line_array)) throw std::runtime_error("lines is not an array");
  size_t line_i, line_max;
  yyjson_val* line_value;
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
      const uint64_t bpm = resolveBpmReference(
          required(note_value, "bpm"), "note.bpm", bpms,
          used_bpm_value_fallback, used_ambiguous_bpm_value_fallback);
      note.start = parseTime(note_value, "startTime", bpm, bpms,
                             used_bpm_value_fallback,
                             used_ambiguous_bpm_value_fallback);
      note.end = parseTime(note_value, "endTime", bpm, bpms,
                           used_bpm_value_fallback,
                           used_ambiguous_bpm_value_fallback);
      const uint64_t type = integer(required(note_value, "type"), "note.type");
      note.type = type == 1 ? NoteType::drag : NoteType::hit;
      note.fake = boolean(required(note_value, "isFake"), "note.isFake");
      note.always_perfect = boolean(required(note_value, "isAlwaysPerfect"), "note.isAlwaysPerfect");
      note.line = static_cast<uint32_t>(line_i);
      if (yyjson_val* index = yyjson_obj_get(note_value, "index")) {
        note.logical_index = integer(index, "note.index");
        note.has_logical_index = true;
      }
      chart.duration = std::max(chart.duration, note.end);
      ++chart.note_count;
    }
  }

  uint32_t missing_note_indices = 0;
  for (const Line& line : chart.lines) {
    for (const Note& note : line.notes) {
      if (!note.has_logical_index) ++missing_note_indices;
    }
  }
  if (missing_note_indices != 0) {
    const std::vector<uint64_t> note_order = extractNoteOrder(ordering_script);
    bool static_order_valid = note_order.size() == chart.note_count;
    std::vector<size_t> line_cursors(chart.lines.size(), 0);
    std::vector<Note*> ordered_notes;
    ordered_notes.reserve(note_order.size());
    for (size_t logical_index = 0;
         static_order_valid && logical_index < note_order.size(); ++logical_index) {
      const uint64_t line_index = note_order[logical_index];
      if (line_index >= chart.lines.size()) {
        static_order_valid = false;
        break;
      }
      Line& line = chart.lines[static_cast<size_t>(line_index)];
      size_t& cursor = line_cursors[static_cast<size_t>(line_index)];
      if (cursor >= line.notes.size()) {
        static_order_valid = false;
        break;
      }
      Note& note = line.notes[cursor++];
      if (note.has_logical_index && note.logical_index != logical_index) {
        static_order_valid = false;
        break;
      }
      ordered_notes.push_back(&note);
    }
    for (size_t line_index = 0;
         static_order_valid && line_index < chart.lines.size(); ++line_index) {
      if (line_cursors[line_index] != chart.lines[line_index].notes.size()) {
        static_order_valid = false;
      }
    }
    if (static_order_valid) {
      for (size_t logical_index = 0; logical_index < ordered_notes.size();
           ++logical_index) {
        ordered_notes[logical_index]->logical_index = logical_index;
        ordered_notes[logical_index]->has_logical_index = true;
      }
      warnings.push_back("restored " + std::to_string(missing_note_indices) +
                         " note indices from static n(...) ordering hints");
    } else {
      uint64_t logical_index = 0;
      for (Line& line : chart.lines) {
        for (Note& note : line.notes) {
          note.logical_index = logical_index++;
          note.has_logical_index = true;
        }
      }
      warnings.push_back(
          "static n(...) ordering hints were incomplete; note indices use JSON traversal order");
    }
  }
  for (Line& line : chart.lines) {
    for (Note& note : line.notes) {
      if (!note.has_logical_index) throw std::runtime_error("missing index");
      note.group = chart.ensureGroup(ObjectType::note, note.logical_index);
    }
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
    storyboard.type = type == 1 ? StoryboardType::text : StoryboardType::picture;
    storyboard.layer = layer == 1
                           ? StoryboardLayer::normal
                           : (layer == 2 ? StoryboardLayer::foreground
                                         : StoryboardLayer::background);
    storyboard.data = string(required(storyboard_value, "data"), "storyboard.data");
    storyboard.group = chart.ensureGroup(ObjectType::storyboard, storyboard_i);
  }

  yyjson_val* animation_array = required(root, "animations");
  if (!yyjson_is_arr(animation_array)) throw std::runtime_error("animations is not an array");
  size_t animation_i, animation_max;
  uint32_t skipped_animations_without_target = 0;
  yyjson_val* animation_value;
  yyjson_arr_foreach(animation_array, animation_i, animation_max, animation_value) {
    if (!yyjson_is_obj(animation_value)) throw std::runtime_error("animation is not an object");
    yyjson_val* target_index = yyjson_obj_get(animation_value, "i1");
    if (!target_index || yyjson_is_null(target_index)) {
      ++skipped_animations_without_target;
      continue;
    }
    const uint64_t raw_key = integer(required(animation_value, "key"), "animation.key");
    const uint64_t raw_target = integer(required(animation_value, "data"), "animation.data");
    const uint64_t key = raw_key <= 23 ? raw_key : 0;
    const uint64_t target = raw_target <= 2 ? raw_target : 0;
    const uint64_t logical_index = integer(target_index, "animation.i1");
    const ObjectType object_type = static_cast<ObjectType>(target);
    const uint32_t group = chart.ensureGroup(object_type, logical_index);
    Event event;
    event.index = static_cast<uint32_t>(animation_i);
    const uint64_t bpm = resolveBpmReference(
        required(animation_value, "bpmId"), "animation.bpmId", bpms,
        used_bpm_value_fallback, used_ambiguous_bpm_value_fallback);
    event.start = parseTime(animation_value, "fromBeat", bpm, bpms,
                            used_bpm_value_fallback,
                            used_ambiguous_bpm_value_fallback);
    event.end = parseTime(animation_value, "toBeat", bpm, bpms,
                          used_bpm_value_fallback,
                          used_ambiguous_bpm_value_fallback);
    event.function = static_cast<uint32_t>(integer(required(animation_value, "press"), "animation.press"));
    event.direction = static_cast<uint32_t>(integer(required(animation_value, "ease"), "animation.ease"));
    event.function = std::clamp(event.function, 0u, 10u);
    event.direction = std::clamp(event.direction, 0u, 2u);
    if (key == 22) {
      const std::string from = string(required(animation_value, "fv"), "animation.fv");
      const std::string to = string(required(animation_value, "tv"), "animation.tv");
      chart.colors.push_back(parseColor(from));
      event.from = static_cast<double>(chart.colors.size());
      chart.colors.push_back(parseColor(to));
      event.to = static_cast<double>(chart.colors.size());
    } else {
      event.from = animationNumber(required(animation_value, "fv"), "animation.fv");
      event.to = animationNumber(required(animation_value, "tv"), "animation.tv");
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
    chart.groups[group].tracks[key].events.push_back(std::move(event));
    chart.duration = std::max(chart.duration, event.end);
    ++chart.animation_count;
  }

  if (used_bpm_value_fallback) {
    warnings.push_back("resolved out-of-range BPM references by matching BPM values");
  }
  if (used_ambiguous_bpm_value_fallback) {
    warnings.push_back(
        "multiple BPM entries matched a BPM value; the first entry was used");
  }
  if (skipped_animations_without_target != 0) {
    warnings.push_back("skipped " +
                       std::to_string(skipped_animations_without_target) +
                       " animations without an i1 target");
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
    line.note_groups.emplace_back().breakable = false;
    std::unordered_map<uint64_t, uint32_t> note_group_map;
    for (uint32_t note_index = 0; note_index < line.notes.size(); ++note_index) {
      Note& note = line.notes[note_index];
      uint64_t bits = 0;
      std::memcpy(&bits, &note.start, sizeof(bits));
      note.simultaneous = simultaneous[bits] > 1;
      note.floor_start = chart.get(line.group, 12, note.start) + chart.get(note.group, 12, note.start);
      note.floor_end = chart.get(line.group, 12, note.end) + chart.get(note.group, 12, note.end);
      const std::optional<uint64_t> animation_hash =
          chart.groups[note.group].noteAnimationHash();
      if (animation_hash) {
        auto found = note_group_map.find(*animation_hash);
        if (found == note_group_map.end()) {
          const uint32_t group_index =
              static_cast<uint32_t>(line.note_groups.size());
          line.note_groups.emplace_back();
          found = note_group_map.emplace(*animation_hash, group_index).first;
        }
        line.note_groups[found->second].indices.push_back(note_index);
      } else {
        line.note_groups[0].indices.push_back(note_index);
      }
      if (!note.fake) {
        chart.combo_times.push_back(note.start);
        if (note.hold()) chart.combo_times.push_back(note.end);
      }
    }
  }
  std::sort(chart.combo_times.begin(), chart.combo_times.end());

  std::mt19937 random_engine{std::random_device{}()};
  std::uniform_real_distribution<double> random_distribution{0.0, 1.0};
  auto uniform = [&](double from, double to) {
    return from + (to - from) * random_distribution(random_engine);
  };
  auto make_particle = [&](HitEffect& effect, double dt = 0.0) {
    Particle& particle = effect.particles.emplace_back();
    particle.dt = dt;
    particle.rotation = uniform(0.0, 360.0);
    particle.initial_speed = uniform(0.3, 0.72);
    particle.initial_size = std::pow(particle.initial_speed, 0.22) *
                            uniform(0.6, 0.7) / 42.0;
    particle.scale = {uniform(1.5, 2.1), uniform(-0.5, 0.5)};
    particle.gravity_coefficient = uniform(0.9, 1.3);
  };
  for (uint32_t line_index = 0; line_index < chart.lines.size(); ++line_index) {
    Line& line = chart.lines[line_index];
    for (uint32_t note_index = 0; note_index < line.notes.size(); ++note_index) {
      const Note& note = line.notes[note_index];
      if (note.fake) continue;
      HitEffect& effect = chart.hit_effects.emplace_back();
      effect.time = note.start;
      effect.texture_rotation = uniform(0.0, 360.0);
      effect.line_index = line_index;
      effect.note_index = note_index;
      if (!note.hold()) {
        for (uint32_t index = 0; index < 10; ++index) make_particle(effect);
      } else {
        double dt = 0.0;
        while (note.start + dt < note.end) {
          make_particle(effect, dt);
          dt += 0.01;
        }
      }
    }
  }
  std::stable_sort(chart.hit_effects.begin(), chart.hit_effects.end(),
                   [](const HitEffect& first, const HitEffect& second) {
                     return first.time < second.time;
                   });
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
struct HitRingPayload {
  float x, y, size, progress, rotation, seed;
  uint32_t rgba;
};
struct ParticlePayload {
  float x, y, radius_x, radius_y, rotation;
  uint32_t rgba;
};
struct TextBitmapPayload {
  float x, y, rotation, pixel_scale_x, pixel_scale_y, anchor_x, anchor_y;
  uint32_t rgba, bitmap_width, bitmap_height, run_count, key_low, key_high;
};
struct TextBitmapRun {
  uint32_t x, y, length, alpha;
};
static_assert(sizeof(RectPayload) == 24);
static_assert(sizeof(QuadPayload) == 36);
static_assert(sizeof(SpritePayload) == 28);
static_assert(sizeof(NotePayload) == 40);
static_assert(sizeof(HitRingPayload) == 28);
static_assert(sizeof(ParticlePayload) == 24);
static_assert(sizeof(TextBitmapPayload) == 52);
static_assert(sizeof(TextBitmapRun) == 16);

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

  template <typename Payload, typename Item>
  void addArray(uint16_t type, uint16_t flags, const Payload& payload,
                const std::vector<Item>& items) {
    static_assert(std::is_trivially_copyable_v<Payload>);
    static_assert(std::is_trivially_copyable_v<Item>);
    CommandHeader header{
        type, flags,
        static_cast<uint32_t>(sizeof(CommandHeader) + sizeof(Payload) +
                              items.size() * sizeof(Item))};
    append(header);
    append(payload);
    if (!items.empty()) {
      const auto* begin = reinterpret_cast<const uint8_t*>(items.data());
      bytes_.insert(bytes_.end(), begin,
                    begin + items.size() * sizeof(Item));
    }
    align();
    ++count_;
  }

  const uint8_t* data() const { return bytes_.data(); }
  uint32_t size() const { return static_cast<uint32_t>(bytes_.size()); }
  uint32_t capacity() const { return static_cast<uint32_t>(bytes_.capacity()); }
  uint32_t count() const { return count_; }

  void append(const CommandBuffer& other) {
    bytes_.insert(bytes_.end(), other.bytes_.begin(), other.bytes_.end());
    count_ += other.count_;
  }

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

class TextRasterizer {
 public:
  struct Bitmap {
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t key = 0;
    std::vector<TextBitmapRun> runs;
  };

  void load(const uint8_t* data, size_t length) {
    if (!data || length == 0) throw std::runtime_error("font data is empty");
    font_data_.assign(data, data + length);
    const int offset = stbtt_GetFontOffsetForIndex(font_data_.data(), 0);
    if (offset < 0 ||
        !stbtt_InitFont(&font_, font_data_.data(), offset)) {
      font_data_.clear();
      throw std::runtime_error("failed to load bundled font");
    }
    loaded_ = true;
    cache_.clear();
  }

  bool loaded() const { return loaded_; }

  const Bitmap& get(std::string_view text, uint32_t font_size) {
    std::string cache_key(text);
    cache_key.append(reinterpret_cast<const char*>(&font_size),
                     sizeof(font_size));
    if (const auto found = cache_.find(cache_key); found != cache_.end()) {
      return found->second;
    }
    if (cache_.size() >= 128) {
      std::uniform_int_distribution<size_t> distribution{0,
                                                         cache_.size() - 1};
      auto victim = cache_.begin();
      std::advance(victim, distribution(random_engine_));
      cache_.erase(victim);
    }
    Bitmap bitmap = render(text, font_size);
    bitmap.key = fnv1a(cache_key);
    return cache_.emplace(std::move(cache_key), std::move(bitmap))
        .first->second;
  }

 private:
  struct Glyph {
    std::vector<uint8_t> alpha;
    int32_t width = 0;
    int32_t height = 0;
    int32_t x_offset = 0;
    int32_t y_offset = 0;
    double advance_width = 0.0;
  };

  std::vector<uint8_t> font_data_;
  stbtt_fontinfo font_{};
  bool loaded_ = false;
  std::unordered_map<std::string, Bitmap> cache_;
  std::mt19937 random_engine_{std::random_device{}()};

  Bitmap render(std::string_view text, uint32_t font_size) {
    std::vector<Glyph> glyphs;
    const float scale =
        stbtt_ScaleForPixelHeight(&font_, static_cast<float>(font_size));
    for (size_t index = 0; index < text.size(); ++index) {
      uint32_t codepoint = 0;
      uint8_t bytes = 0;
      const uint8_t character = static_cast<uint8_t>(text[index]);
      if ((character & 0x80u) == 0) {
        codepoint = character;
        bytes = 1;
      } else if ((character & 0xe0u) == 0xc0u) {
        codepoint = character & 0x1fu;
        bytes = 2;
      } else if ((character & 0xf0u) == 0xe0u) {
        codepoint = character & 0x0fu;
        bytes = 3;
      } else if ((character & 0xf8u) == 0xf0u) {
        codepoint = character & 0x07u;
        bytes = 4;
      }
      if (bytes == 0 || index + bytes > text.size()) break;
      for (uint8_t continuation = 1; continuation < bytes; ++continuation) {
        codepoint =
            (codepoint << 6) |
            (static_cast<uint8_t>(text[index + continuation]) & 0x3fu);
      }
      index += bytes - 1;
      int glyph_index = stbtt_FindGlyphIndex(&font_, codepoint);
      if (glyph_index == 0) glyph_index = stbtt_FindGlyphIndex(&font_, '?');
      if (glyph_index == 0) continue;

      Glyph& glyph = glyphs.emplace_back();
      int advance = 0;
      int left_side_bearing = 0;
      stbtt_GetGlyphHMetrics(&font_, glyph_index, &advance,
                             &left_side_bearing);
      glyph.advance_width = advance * scale;
      int right = 0;
      int bottom = 0;
      stbtt_GetGlyphBitmapBox(&font_, glyph_index, scale, scale,
                              &glyph.x_offset, &glyph.y_offset, &right,
                              &bottom);
      glyph.width = right - glyph.x_offset;
      glyph.height = bottom - glyph.y_offset;
      if (glyph.width <= 0 || glyph.height <= 0) continue;
      glyph.alpha.resize(static_cast<size_t>(glyph.width) * glyph.height);
      stbtt_MakeGlyphBitmap(&font_, glyph.alpha.data(), glyph.width,
                            glyph.height, glyph.width, scale, scale,
                            glyph_index);
    }
    if (glyphs.empty()) return {.width = 2, .height = 2};

    int32_t top = 0;
    int32_t bottom = 0;
    double width = 0.0;
    double real_right = 0.0;
    for (const Glyph& glyph : glyphs) {
      top = std::min(top, glyph.y_offset);
      bottom = std::max(bottom, glyph.y_offset + glyph.height);
      real_right =
          std::max(real_right, width + glyph.x_offset + glyph.width);
      width += glyph.advance_width;
      real_right = std::max(real_right, width);
    }
    constexpr int32_t padding = 2;
    const int32_t bitmap_width = static_cast<int32_t>(
        std::ceil(real_right - glyphs.front().x_offset + padding * 2));
    if (top >= bottom || bitmap_width <= 0) {
      return {.width = 2, .height = 2};
    }
    const int32_t bitmap_height = bottom - top;
    std::vector<uint8_t> alpha(
        static_cast<size_t>(bitmap_width) * bitmap_height, 0);
    double x = -glyphs.front().x_offset + padding;
    for (const Glyph& glyph : glyphs) {
      const int64_t target_y = glyph.y_offset - top;
      const int64_t target_x = static_cast<int64_t>(std::ceil(x + glyph.x_offset));
      for (int32_t source_x = 0; source_x < glyph.width; ++source_x) {
        const int64_t destination_x = target_x + source_x;
        if (destination_x < 0) continue;
        if (destination_x >= bitmap_width) break;
        for (int32_t source_y = 0; source_y < glyph.height; ++source_y) {
          const int64_t destination_y = target_y + source_y;
          if (destination_y < 0) continue;
          if (destination_y >= bitmap_height) break;
          const uint8_t source_alpha =
              glyph.alpha[static_cast<size_t>(source_y) * glyph.width +
                          source_x];
          uint8_t& destination_alpha =
              alpha[static_cast<size_t>(destination_y) * bitmap_width +
                    destination_x];
          const double source = source_alpha / 255.0;
          const double destination = destination_alpha / 255.0;
          destination_alpha = static_cast<uint8_t>(
              (source + destination * (1.0 - source)) * 255.0);
        }
      }
      x += glyph.advance_width;
    }

    Bitmap bitmap{.width = static_cast<uint32_t>(bitmap_width),
                  .height = static_cast<uint32_t>(bitmap_height)};
    for (uint32_t y = 0; y < bitmap.height; ++y) {
      uint32_t x_index = 0;
      while (x_index < bitmap.width) {
        const uint8_t value = alpha[static_cast<size_t>(y) * bitmap.width +
                                    x_index];
        if (value == 0) {
          ++x_index;
          continue;
        }
        uint32_t end = x_index + 1;
        while (end < bitmap.width &&
               alpha[static_cast<size_t>(y) * bitmap.width + end] == value) {
          ++end;
        }
        bitmap.runs.push_back(
            {.x = x_index, .y = y, .length = end - x_index, .alpha = value});
        x_index = end;
      }
    }
    return bitmap;
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
  CommandBuffer track_commands;
  CommandBuffer hit_ring_commands;
  std::array<CommandBuffer, 3> note_commands;
  CommandBuffer particle_commands;
  TextRasterizer text_rasterizer;
  double hit_ring_seed = [] {
    std::mt19937 engine{std::random_device{}()};
    return std::uniform_real_distribution<double>{0.0, 1.0}(engine);
  }();
  std::vector<std::string> warnings;
  std::string last_error;

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
    // VisibleArea is expressed relative to the 1080-unit world viewport.
    if (floor.x / kWorldHeight > visible_area) alpha = 0.0;
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
    return frame;
  }

  void addText(Vec2 position, double size, std::string_view text, uint32_t rgba,
               double anchor_x = 0.5, double anchor_y = 0.5,
               double rotation = 0.0, double scale_x = 1.0, double scale_y = 1.0) {
    if (size <= 0.0) return;
    if (!text_rasterizer.loaded()) {
      throw std::runtime_error("native text rasterizer is not initialized");
    }
    const double width_scale = std::min(std::max(scale_x, scale_y), 16.0);
    if (width_scale > 1.0) {
      scale_x /= width_scale;
      scale_y /= width_scale;
      size *= width_scale;
    }
    const uint32_t raster_size =
        static_cast<uint32_t>(std::ceil(size / 48.0) * 48.0);
    const TextRasterizer::Bitmap& bitmap =
        text_rasterizer.get(text, raster_size);
    const double texture_scale = size / raster_size;
    commands.addArray(
        PLUVIORA_COMMAND_TEXT_BITMAP, 0,
        TextBitmapPayload{
            static_cast<float>(position.x), static_cast<float>(position.y),
            static_cast<float>(rotation),
            static_cast<float>(texture_scale * scale_x),
            static_cast<float>(texture_scale * scale_y),
            static_cast<float>(anchor_x), static_cast<float>(anchor_y), rgba,
            bitmap.width, bitmap.height,
            static_cast<uint32_t>(bitmap.runs.size()),
            static_cast<uint32_t>(bitmap.key),
            static_cast<uint32_t>(bitmap.key >> 32)},
        bitmap.runs);
  }

  void addStoryboards(StoryboardLayer layer, double time, float width,
                      float height) {
    Chart& value = *chart;
    for (Storyboard& storyboard : value.storyboards) {
      if (storyboard.layer != layer ||
          storyboard.type == StoryboardType::picture) {
        continue;
      }
      const Vec2 position = value.position(storyboard.group, time, width, height);
      const double alpha = value.get(storyboard.group, 2, time);
      const double size = value.get(storyboard.group, 3, time);
      const double rotation = -value.get(storyboard.group, 4, time);
      const double sb_width = value.get(storyboard.group, 10, time);
      const double sb_height = value.get(storyboard.group, 11, time);
      const Color color = value.color(storyboard.group, time);
      if (alpha * color.a <= 0.0) continue;
      addText(position, (width + height) * 0.025 * size, storyboard.data,
              color.rgba(alpha), 0.5, 0.5, rotation, sb_width, sb_height);
    }
  }

  static Color particleRgb(double progress) {
    const Color start{142.0 / 255.0, 197.0 / 255.0, 252.0 / 255.0, 1.0};
    const Color end{162.0 / 255.0, 66.0 / 255.0, 255.0 / 255.0, 1.0};
    if (progress <= 0.0) return start;
    if (progress >= 0.75) return end;
    return lerp(start, end, progress / 0.75);
  }

  static double particleAlpha(double progress) {
    if (progress <= 0.0 || progress >= 1.0) return 0.0;
    if (progress < 0.128) return progress / 0.128;
    if (progress <= 0.805) return 1.0;
    return (1.0 - progress) / (1.0 - 0.805);
  }

  void addHitEffects(double time, float width, float height,
                     double line_head_base, const Rect& screen) {
    Chart& value = *chart;
    for (uint32_t effect_index = value.first_hit_effect_index;
         effect_index < value.hit_effects.size(); ++effect_index) {
      HitEffect& effect = value.hit_effects[effect_index];
      if (effect.time > time) break;
      Line& line = value.lines[effect.line_index];
      Note& note = line.notes[effect.note_index];
      NoteFrame frame = noteFrame(line, note, time, width, height);
      if (effect.endTime(kHitDuration) < time) {
        value.passedHitEffectIndex(effect_index);
        continue;
      }
      const double ring_progress = (time - effect.time) / kHitDuration;
      if (ring_progress <= 1.0) {
        const double ring_size =
            line_head_base * 4.632 *
            (1.0 - std::pow(1.0 - ring_progress, 3.0)) * frame.scale;
        const auto ring_quad =
            makeQuad(frame.head, {ring_size, ring_size}, effect.texture_rotation);
        if (quadStrictlyIntersectsRect(ring_quad, screen)) {
          Color ring_color = particleRgb(0.065 + ring_progress * 0.4);
          ring_color.a = 1.0;
          hit_ring_commands.add(
              PLUVIORA_COMMAND_HIT_RING, 0,
              HitRingPayload{static_cast<float>(frame.head.x),
                             static_cast<float>(frame.head.y),
                             static_cast<float>(ring_size),
                             static_cast<float>(ring_progress),
                             static_cast<float>(effect.texture_rotation),
                             static_cast<float>(hit_ring_seed),
                             ring_color.rgba()});
        }
      }
      for (const Particle& particle : effect.particles) {
        const double particle_time = effect.time + particle.dt;
        if (particle_time > time) break;
        if (particle_time + kHitDuration < time) continue;
        const double progress = std::clamp(
            (time - particle_time) / kHitDuration, 0.0, 1.0);
        const double note_scaling = frame.scale * value.note_scale;
        const double base_size = particle.initial_size * (width + height);
        const double radius = particle.radius(progress) * (width + height);
        Vec2 position =
            frame.head + rotate({radius * note_scaling, 0.0}, particle.rotation);
        position.y += particle.deltaY(progress) * (width + height) * note_scaling;
        double rotation = particle.rotation;
        rotation +=
            (rotation - (position - frame.head).atanDegrees()) * 2.0;
        const Vec2 particle_radius =
            particle.particleScale(progress) * base_size * note_scaling;
        if (!quadStrictlyIntersectsRect(
                makeQuad(position, particle_radius * 2.0, rotation), screen)) {
          continue;
        }
        Color color = particleRgb(progress);
        color.a = particleAlpha(progress);
        particle_commands.add(
            PLUVIORA_COMMAND_PARTICLE, 0,
            ParticlePayload{static_cast<float>(position.x),
                            static_cast<float>(position.y),
                            static_cast<float>(particle_radius.x),
                            static_cast<float>(particle_radius.y),
                            static_cast<float>(rotation), color.rgba()});
      }
    }
  }

  struct NoteTextureSize {
    double width = 0.0;
    double head = 0.0;
    double tail = 0.0;
  };

  static Vec2 noteTextureDimensions(const Note& note) {
    if (note.type == NoteType::drag) return {1072.0, 1016.0};
    if (note.hold()) {
      if (note.always_perfect) return {4036.0, 1340.0};
      return note.simultaneous ? Vec2{4036.0, 1344.0}
                               : Vec2{4036.0, 1336.0};
    }
    if (note.always_perfect) {
      return note.simultaneous ? Vec2{1340.0, 1344.0}
                               : Vec2{1340.0, 1340.0};
    }
    return note.simultaneous ? Vec2{1340.0, 1340.0}
                             : Vec2{1300.0, 1296.0};
  }

  static NoteTextureSize noteTextureSize(const Note& note,
                                         double line_head_base) {
    const Vec2 dimensions = noteTextureDimensions(note);
    const double cut_padding = note.hold() ? 668.0 : dimensions.x / 2.0;
    const double total_height =
        line_head_base / dimensions.y * dimensions.x * 1.77;
    return {.width = line_head_base * 1.77,
            .head = cut_padding / dimensions.x * total_height,
            .tail = cut_padding / dimensions.x * total_height};
  }

  static double maxNoteHeadHalfDiagonal(double line_head_base) {
    const std::array<Vec2, 8> textures{
        Vec2{1300.0, 1296.0}, Vec2{1340.0, 1340.0},
        Vec2{1072.0, 1016.0}, Vec2{4036.0, 1336.0},
        Vec2{4036.0, 1344.0}, Vec2{1340.0, 1344.0},
        Vec2{4036.0, 1340.0}, Vec2{4036.0, 1340.0},
    };
    double maximum = 0.0;
    for (const Vec2& dimensions : textures) {
      const bool hold = dimensions.x == 4036.0;
      const double cut_padding = hold ? 668.0 : dimensions.x / 2.0;
      const double total_height =
          line_head_base / dimensions.y * dimensions.x * 1.77;
      const double head = cut_padding / dimensions.x * total_height;
      maximum = std::max(
          maximum,
          std::hypot(line_head_base * 1.77, head) / 2.0);
    }
    return maximum;
  }

  PluvioraStatus render(double time, float width, float height, double song_length,
                        PluvioraFrameView* output) {
    if (!chart) return PLUVIORA_NOT_LOADED;
    if (!output || !std::isfinite(time) || width <= 0.0f || height <= 0.0f) {
      return PLUVIORA_INVALID_ARGUMENT;
    }
    Chart& value = *chart;
    commands.clear();
    track_commands.clear();
    hit_ring_commands.clear();
    for (CommandBuffer& note_buffer : note_commands) note_buffer.clear();
    particle_commands.clear();
    uint32_t hit_count = 0;
    uint32_t drag_count = 0;
    uint32_t fracture_count = 0;
    const Rect screen{0.0, 0.0, width, height};
    const double line_head_base = (width + height) * 0.0223 * value.note_scale;
    const double max_head_half_diagonal =
        maxNoteHeadHalfDiagonal(line_head_base);
    value.timeUpdated(time);

    addStoryboards(StoryboardLayer::background, time, width, height);
    commands.add(PLUVIORA_COMMAND_RECT, 0,
                 RectPayload{width * 0.5f, height * 0.5f, width, height, 0.0f,
                             Color{0.0, 0.0, 0.0, 0.8}.rgba()});

    for (Line& line : value.lines) {
      const Vec2 position = value.position(line.group, time, width, height);
      const double rotation = value.get(line.group, 4, time);
      const double alpha = value.get(line.group, 2, time);
      const double head_alpha = value.get(line.group, 9, time) * alpha;
      const double body_alpha = value.get(line.group, 8, time) * alpha;
      const double line_size = value.get(line.group, 3, time);
      const Color line_color = value.color(line.group, time);
      const double line_head_size = line_head_base * line_size;
      if (head_alpha > 0.0 &&
          quadStrictlyIntersectsRect(
              makeQuad(position, {line_head_size, line_head_size},
                       180.0 - rotation),
              screen)) {
        track_commands.add(
            PLUVIORA_COMMAND_SPRITE, 0,
            SpritePayload{static_cast<float>(position.x),
                          static_cast<float>(position.y),
                          static_cast<float>(line_head_size),
                          static_cast<float>(line_head_size),
                          static_cast<float>(180.0 - rotation),
                          line_color.rgba(head_alpha),
                          PLUVIORA_SPRITE_LINE_HEAD});
      }
      if (body_alpha > 0.0) {
        const double connect = line_head_base * (334.0 / 744.0);
        const double line_width = line_head_base * 0.096774;
        auto transform = [&](Vec2 point) {
          return position + rotate(point * line_size, -rotation);
        };
        const std::array<Vec2, 4> points{
            transform({connect, -line_width / 2.0}),
            transform({connect + height * 2.5, -line_width / 2.0}),
            transform({connect + height * 2.5, line_width / 2.0}),
            transform({connect, line_width / 2.0}),
        };
        if (quadStrictlyIntersectsRect(points, screen)) {
          track_commands.add(
              PLUVIORA_COMMAND_QUAD, 0,
              QuadPayload{static_cast<float>(points[0].x),
                          static_cast<float>(points[0].y),
                          static_cast<float>(points[1].x),
                          static_cast<float>(points[1].y),
                          static_cast<float>(points[2].x),
                          static_cast<float>(points[2].y),
                          static_cast<float>(points[3].x),
                          static_cast<float>(points[3].y),
                          line_color.rgba(body_alpha)});
        }
      }

      for (NoteGroup& note_group : line.note_groups) {
        note_group.timeUpdated(time);
        for (uint32_t group_note_index = note_group.first_note_index;
             group_note_index < note_group.indices.size(); ++group_note_index) {
          Note& note = line.notes[note_group.indices[group_note_index]];
          note.timeUpdated(time);
          NoteFrame frame = noteFrame(line, note, time, width, height);
          if (time >= note.start && note.onPlayHitsound() && !note.fake) {
            if (note.type == NoteType::hit) {
              ++hit_count;
            } else if (note.type == NoteType::drag) {
              ++drag_count;
            } else {
              ++fracture_count;
            }
          }
          if (note.end < time &&
              (!note.hold() || note.end + 0.2 <= time)) {
            note_group.passedNoteIndex(group_note_index);
            continue;
          }
          if (note.hold()) {
            frame.alpha *=
                1.0 - std::clamp((time - note.end) / 0.2, 0.0, 1.0);
          }
          NoteTextureSize texture_size =
              noteTextureSize(note, line_head_base);
          double body =
              std::min(8192.0, (frame.head - frame.tail).length());
          texture_size.width *= frame.scale;
          texture_size.head *= frame.scale;
          texture_size.tail *= frame.scale;
          body *= frame.scale;
          auto note_transform = [&](Vec2 point) {
            point.y *= -1.0;
            return frame.head + rotate(point, frame.rotation);
          };
          const std::array<Vec2, 4> note_quad{
              note_transform({-texture_size.head,
                              -texture_size.width / 2.0}),
              note_transform({-texture_size.head,
                              texture_size.width / 2.0}),
              note_transform({body + texture_size.tail,
                              texture_size.width / 2.0}),
              note_transform({body + texture_size.tail,
                              -texture_size.width / 2.0}),
          };
          const Rect extended_screen =
              screen.extend(max_head_half_diagonal * frame.scale);
          if (quadStrictlyIntersectsRect(note_quad, extended_screen)) {
            if (frame.alpha <= 0.0) continue;
            uint32_t kind = PLUVIORA_NOTE_TAP;
            uint32_t final_type = 1;
            if (note.hold()) {
              kind = PLUVIORA_NOTE_HOLD;
              final_type = 0;
            } else if (note.type == NoteType::drag) {
              kind = PLUVIORA_NOTE_DRAG;
              final_type = 2;
            }
            uint32_t note_flags = 0;
            if (note.simultaneous) note_flags |= 1u;
            if (note.always_perfect) note_flags |= 2u;
            if (note.fake) note_flags |= 4u;
            note_commands[final_type].add(
                PLUVIORA_COMMAND_NOTE, 0,
                NotePayload{static_cast<float>(frame.head.x),
                            static_cast<float>(frame.head.y),
                            static_cast<float>(frame.rotation),
                            static_cast<float>(texture_size.width),
                            static_cast<float>(texture_size.head),
                            static_cast<float>(body),
                            static_cast<float>(texture_size.tail),
                            frame.color.rgba(frame.alpha), kind, note_flags});
          } else if (note_group.breakable &&
                     lineIsLeavingScreen(frame.head,
                                         frame.speed_rotation + 90.0,
                                         extended_screen) &&
                     lineIsLeavingScreen(frame.head,
                                         frame.rotation + 90.0,
                                         extended_screen)) {
            break;
          }
        }
      }
    }

    addHitEffects(time, width, height, line_head_base, screen);
    addStoryboards(StoryboardLayer::normal, time, width, height);
    commands.append(track_commands);
    commands.append(hit_ring_commands);
    commands.append(note_commands[0]);
    commands.append(note_commands[1]);
    commands.append(note_commands[2]);
    commands.append(particle_commands);
    addStoryboards(StoryboardLayer::foreground, time, width, height);

    const double safe_song_length = song_length > 0.0 ? song_length : std::max(value.duration, 1.0);
    const double song_progress = time / safe_song_length;
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
    const double target_score =
        value.combo_times.empty()
            ? 1010000.0
            : std::clamp(
                  std::ceil(1010000.0 / value.combo_times.size() * combo),
                  0.0, 1010000.0);
    const uint64_t animated_score = static_cast<uint64_t>(
        value.score_animation.weakSet(time, target_score).get(time));
    std::string score_text = std::to_string(animated_score);
    if (score_text.size() < 7) score_text.insert(0, 7 - score_text.size(), '0');
    addText({width * 0.9752375, width * 0.0395833}, width * 0.0268352,
            score_text, 0xffffffffu, 1.0, 0.5);
    addText({width * 0.9752375, width * 0.06684375}, width * 0.0201352,
            "100.00%", 0xffffffbfu, 1.0, 0.5);
    addText({width * 0.5, width * 0.0359375}, width * 0.0201352,
            "ALL PERFECT", 0xffffffffu);
    const double combo_scale =
        value.combo_scale_animation.weakSet(time, combo).get(time);
    addText({width * 0.5, width * 0.0677083},
            width * 0.0263352 * combo_scale,
            std::to_string(combo), 0xffffffffu);
    commands.add(PLUVIORA_COMMAND_RECT, 1,
                 RectPayload{static_cast<float>(song_progress * width * 0.5),
                             width * 0.00234375f,
                             static_cast<float>(song_progress * width),
                             width * 0.0046875f, 0.0f, 0xffffffffu});

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
    const std::string_view ordering_script(
        js_data ? reinterpret_cast<const char*>(js_data) : "", js_length);
    pluviora::Chart parsed =
        pluviora::parseChart(json, ordering_script, engine->warnings);
    engine->chart = std::move(parsed);
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

PluvioraStatus pluviora_seek(PluvioraHandle handle, double time_seconds) {
  auto* engine = static_cast<pluviora::Engine*>(handle);
  return pluviora::protect(engine, [&] {
    if (!engine->chart) return PLUVIORA_NOT_LOADED;
    if (!std::isfinite(time_seconds)) return PLUVIORA_INVALID_ARGUMENT;
    engine->chart->seek(time_seconds);
    return PLUVIORA_OK;
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

PluvioraStatus pluviora_set_font_data(PluvioraHandle handle,
                                      const uint8_t* font_data,
                                      size_t font_length) {
  auto* engine = static_cast<pluviora::Engine*>(handle);
  if (!engine) return PLUVIORA_INVALID_HANDLE;
  if (!font_data || font_length == 0) return PLUVIORA_INVALID_ARGUMENT;
  return pluviora::protect(engine, [&] {
    engine->text_rasterizer.load(font_data, font_length);
    return PLUVIORA_OK;
  });
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
