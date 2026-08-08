## 1.0.2

- Replaced per-frame audio-position polling with a local timestamp playback
  clock and synchronized audio only at load, play, pause, seek, and rate-change
  boundaries.
- Added monotonic clock-correction guards, safe completed-playback handle
  recreation, and race-free cleanup during rapid source reloads.
- Aligned the minimum playback rate with the audio backend's `0.05` limit.

## 1.0.1

- Statically linked the Android C++ runtime into the generated native asset so
  consumer applications can load `libpluviora.so` without separately bundling
  `libc++_shared.so`.
- Added release APK linkage checks for every generated Android ABI.

## 1.0.0

- Promoted Pluviora to its first stable release.
- Fixed hit-ring fragment output to use premultiplied alpha, removing the
  purple bounding square on Impeller while preserving the circular expansion
  and splash particles.
- Added native regression coverage to keep both hit-ring and particle commands
  active after a note hit.

## 0.4.1

- Fixed visible-area unit handling so tap and drag notes remain visible
  while approaching the judgment line.
- Synchronized native playback state after explicit seeks to prevent skipped
  hit sounds from being replayed together on the next frame.
- Deferred player loads requested during widget builds until after the frame so
  controller notifications cannot trigger build-phase rebuild assertions.

## 0.4.0

- Reworked native playback and rendering behavior to follow the licensed
  reference implementation, including animation state, clipping, draw order,
  score/combo transitions, hit effects, and rewind-aware sound triggers.
- Added bundled full-resolution runtime textures, font, and sound effects.
- Replaced Flutter text layout with native `stb_truetype` rasterization and
  RLE bitmap-text commands.
- Added strict static `n(...)` note-order recovery without executing
  JavaScript. Incomplete hints fall back to JSON traversal order with a
  warning.
- Added numeric BPM matching for out-of-range references and aggregate warning
  handling for animation entries without a non-null `i1` target.
- Removed custom picture-storyboard assets and retained the reference no-op
  behavior for picture entries.

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
