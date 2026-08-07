#include <flutter/runtime_effect.glsl>

uniform vec2 uSize;
uniform float uProgress;
uniform float uSeed;
uniform vec4 uColor;

out vec4 fragColor;

float rand(vec2 n) {
  return fract(sin(dot(n, vec2(12.9898, 78.233))) * 43758.5453);
}

float noise(vec2 p) {
  vec2 ip = floor(p);
  vec2 fp = fract(p);
  float a = rand(ip);
  float b = rand(ip + vec2(1.0, 0.0));
  float c = rand(ip + vec2(0.0, 1.0));
  float d = rand(ip + vec2(1.0, 1.0));
  vec2 u = fp * fp * (3.0 - 2.0 * fp);
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float circularNoise(vec2 uv, float density, float seed) {
  vec2 center = uv - 0.5;
  float radius = length(center) * density;
  float angle = abs(atan(center.y, center.x));
  if (uv.y > 0.5) {
    angle += sin(angle) * 2.0;
  }
  vec2 seedOffset = vec2(seed * 100.0, seed * 100.0);
  vec2 polarCoord = vec2(radius, angle) + seedOffset;
  float value = 0.0;
  value += noise(polarCoord) * 0.7;
  value += noise(polarCoord * 2.0) * 0.3;
  value += noise(polarCoord * 4.0) * 0.1;
  return value;
}

void main() {
  vec2 uv = FlutterFragCoord().xy / uSize;
  float radius = length(uv - 0.5);
  float alpha = 0.0;
  if (0.43 <= radius && radius <= 0.5) {
    float value = circularNoise(uv, 50.0, uSeed);
    alpha = value < uProgress ? 0.0 : 1.0;
  }
  fragColor = vec4(uColor.rgb, uColor.a * alpha);
}
