#include <flutter/runtime_effect.glsl>

uniform vec2 uSize;
uniform float uProgress;
uniform vec4 uColor;

out vec4 fragColor;

void main() {
  vec2 uv = FlutterFragCoord().xy / uSize;
  float distanceFromCenter = length(uv - vec2(0.5));
  float radius = mix(0.18, 0.47, uProgress);
  float thickness = mix(0.075, 0.012, uProgress);
  float ring = 1.0 - smoothstep(thickness, thickness + 0.012,
                                abs(distanceFromCenter - radius));
  float glow = 1.0 - smoothstep(thickness * 2.5, thickness * 5.0,
                                abs(distanceFromCenter - radius));
  fragColor = vec4(uColor.rgb, uColor.a * (ring + glow * 0.28) *
                                  (1.0 - uProgress));
}
