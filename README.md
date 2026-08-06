# Pluviora

A Flutter Cross-Platform Library for Special File Previewing

[![CI](https://github.com/jiangyin14/Pluviora/actions/workflows/ci.yml/badge.svg)](https://github.com/jiangyin14/Pluviora/actions/workflows/ci.yml)

Pluviora combines an independent C++20 parsing and frame-generation core with
Flutter audio, image decoding, and Canvas rendering. It is designed for apps
that need deterministic, high-performance previews of supported JSON documents
with synchronized audio and optional companion resources.

Documentation: [Quick start](https://github.com/jiangyin14/Pluviora/blob/main/doc/quick_start.md) ·
[API guide](https://github.com/jiangyin14/Pluviora/blob/main/doc/api.md) ·
[Architecture and ABI](https://github.com/jiangyin14/Pluviora/blob/main/doc/architecture.md)

## Features

- Android and iOS support through Flutter and C++ FFI.
- Independent engine handles with no process-wide player singleton.
- Native-owned, dynamically sized drawing-command buffers.
- Audio-position master clock with play, pause, seek, rate, and volume control.
- File-backed or memory-backed JSON and audio inputs.
- Optional static ordering hints, backgrounds, and named overlay images.
- Deterministic effects and neutral assets generated inside this repository.

## Requirements

- Flutter 3.38+
- Dart 3.10.8+
- Android API 24+
- iOS 13+

## Installation

```bash
flutter pub add pluviora
```

Or add the package directly to `pubspec.yaml`:

```yaml
dependencies:
  pluviora: ^0.2.0
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
  overlayAssets: {
    'overlay.png': '/path/to/overlay.png',
  },
);

PluvioraPlayer(
  source: source,
  controller: controller,
  autoplay: true,
  onLoaded: (result) => print('Loaded ${result.metadata.title}'),
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

## Input model

The package accepts a supported JSON preview document and an audio file. An
optional JavaScript companion may provide static ordering hints; it is scanned
as text and is never executed. Background and named overlay images are
optional. The core package only accepts paths or bytes, while file selection is
kept in the example app.

The JSON root must contain these fields:

| Field | Shape | Purpose |
| --- | --- | --- |
| `meta` | object | Title, credits, difficulty, and optional asset names |
| `bpms` | array | Timing segments with `start` and `bpm` values |
| `lines` | array | Preview lines and their `notes` arrays |
| `animations` | array | Time-based property changes for preview objects |
| `storyboardObjects` | array | Optional visual objects; use an empty array when unused |

See the [quick start](https://github.com/jiangyin14/Pluviora/blob/main/doc/quick_start.md#supported-document-structure)
for a minimal valid document and input ownership details.

## Architecture

```text
JSON document + optional ordering hints
                  │
                  ▼
          C++20 + yyjson
 parsing, animation, geometry, clipping
                  │ one synchronous FFI call per frame
                  ▼
 native-owned drawing-command buffer
                  │
                  ▼
 Flutter CustomPainter + audio master clock
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

Pluviora is available under the
[MIT License](https://github.com/jiangyin14/Pluviora/blob/main/LICENSE).
Third-party dependency notices are listed in
[THIRD_PARTY_NOTICES.md](https://github.com/jiangyin14/Pluviora/blob/main/THIRD_PARTY_NOTICES.md).
