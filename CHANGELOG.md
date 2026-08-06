## 0.2.2

- Removed the mandatory AVIF plugin dependency so Pluviora can coexist with
  applications using newer protobuf and `flutter_rust_bridge` releases.
- Background images now use Flutter's platform image decoder and still fall
  back to the default background when a format is unavailable.

## 0.2.1

- Relaxed the AVIF decoder dependency to remain compatible with applications
  using newer protobuf releases, without changing the public player API.

## 0.2.0

- Added the first public Flutter API for special-file previewing.
- Added `PluvioraSource`, `PluvioraPlayer`, `PluvioraController`, and the
  low-level `PluvioraEngine` API.
- Added independent C ABI v1 engine instances, JSON parsing, and dynamically
  sized zero-copy frame views.
- Added an audio-position master clock, AVIF backgrounds, named overlay assets,
  a mobile sprite atlas, and a fragment-shader effect.
- Added generated neutral visual and audio assets.
- Added native sanitizer tests, Flutter tests, golden coverage, and mobile
  release-build CI.
