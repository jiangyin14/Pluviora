# Pluviora

A Flutter Cross-Platform Library for Special File Previewing

[![CI](https://github.com/jiangyin14/Pluviora/actions/workflows/ci.yml/badge.svg)](https://github.com/jiangyin14/Pluviora/actions/workflows/ci.yml)

Pluviora combines an independent C++20 parsing and frame-generation core with
Flutter audio, image decoding, and Canvas rendering. It is intended for mobile
apps that need high-performance previews of the supported JSON format with a
synchronized audio track and optional companion inputs.

Documentation: [Quick start](https://github.com/jiangyin14/Pluviora/blob/main/doc/quick_start.md) ·
[API guide](https://github.com/jiangyin14/Pluviora/blob/main/doc/api.md) ·
[Architecture and ABI](https://github.com/jiangyin14/Pluviora/blob/main/doc/architecture.md)

## Features

- Android and iOS support through Flutter and C++ FFI.
- Independent engine handles with no process-wide player singleton.
- Native-owned, dynamically sized drawing-command buffers.
- Local timestamp master clock with event-boundary audio synchronization and
  play, pause, seek, rate, and volume control.
- File-backed or memory-backed JSON and audio inputs.
- Optional static `n(...)` ordering hints and AVIF/PNG/JPEG/WebP backgrounds.
- Bundled full-resolution runtime textures, font, and sound effects.
- Native `stb_truetype` text rasterization and a Flutter fragment-shader hit
  effect.

## Requirements

- Flutter 3.38+
- Dart 3.10.8+
- Android and iOS projects supported by the selected Flutter release
- iOS 13+

## Installation

```bash
flutter pub add pluviora
```

## Quick start

```dart
import 'package:pluviora/pluviora.dart';

final controller = PluvioraController();

final source = PluvioraSource.files(
  document: '/path/to/preview.json',
  audio: '/path/to/audio.ogg',
  orderingScript: '/path/to/ordering.js',
  background: '/path/to/background.avif',
);

PluvioraPlayer(
  source: source,
  controller: controller,
  autoplay: true,
  onLoaded: (result) {
    for (final warning in result.warnings) {
      print(warning);
    }
  },
);
```

The player must receive bounded layout constraints, for example from a page
body, `Expanded`, `AspectRatio`, or `SizedBox`.

```dart
await controller.pause();
await controller.seek(const Duration(seconds: 30));
await controller.setPlaybackRate(1.25);
await controller.setMusicVolume(0.8);
await controller.setSfxVolume(0.6);
await controller.setNoteScale(1.1);
await controller.play();
```

Dispose controllers created by your application.

## Input behavior

The package accepts a supported JSON preview document and an audio file. An
optional JavaScript companion is treated as untrusted text: Pluviora only scans
literal `n(<line>, ...)` calls to recover missing note indices and never
executes JavaScript. If the hints are missing or incomplete, notes are indexed
in JSON traversal order and a non-fatal warning is returned.

Animation targets must include a non-null `i1`. Entries without a target are
skipped and summarized in one warning so they cannot affect another object.
Out-of-range BPM references are first matched against BPM values; ambiguous
matches use the first entry and return a warning.

An optional background can be supplied separately. Picture storyboard entries
are intentionally ignored to match the reference renderer; text storyboard
entries remain supported. The core package only accepts paths or bytes, while
file selection is kept in the example app.

The JSON root must contain these fields:

| Field | Shape | Purpose |
| --- | --- | --- |
| `meta` | object | Title, credits, difficulty, and optional asset names |
| `bpms` | array | Timing segments with `start` and `bpm` values |
| `lines` | array | Preview lines and their `notes` arrays |
| `animations` | array | Time-based property changes for preview objects |
| `storyboardObjects` | array | Optional text or picture entries |

See the [quick start](https://github.com/jiangyin14/Pluviora/blob/main/doc/quick_start.md#supported-document-structure)
for a minimal valid document and input ownership details.

## Architecture

```text
JSON document + optional static ordering hints
                  │
                  ▼
          C++20 + yyjson + stb_truetype
 parsing, animation, geometry, clipping, text rasterization
                  │ one synchronous FFI call per frame
                  ▼
 native-owned drawing-command buffer
                  │
                  ▼
 Flutter CustomPainter + local timestamp master clock
```

The native core has no filesystem, image-decoder, audio-backend, OpenGL, GLFW,
or desktop-window dependency. Exceptions are contained at the C ABI boundary
and exposed as status codes.

## Validation

```bash
flutter analyze
flutter test --exclude-tags golden
flutter test test/pluviora_golden_test.dart
dart pub publish --dry-run
```

## License

Pluviora source code is available under the
[MIT License](https://github.com/jiangyin14/Pluviora/blob/main/LICENSE).
Bundled assets and third-party components may have separate terms; see
[THIRD_PARTY_NOTICES.md](https://github.com/jiangyin14/Pluviora/blob/main/THIRD_PARTY_NOTICES.md).
