# Pluviora API guide

## Main types

| Type | Purpose |
| --- | --- |
| `PluvioraAsset` | A path-backed or memory-backed input |
| `PluvioraSource` | Document, audio, and optional companion resources |
| `PluvioraPlayer` | A self-contained preview widget |
| `PluvioraController` | Playback control and observable state |
| `PluvioraEngine` | Low-level native engine without audio or widgets |
| `PluvioraLoadResult` | Metadata and non-fatal warnings |
| `PluvioraFrameView` | A zero-copy native drawing-command view |

## Source

```dart
PluvioraSource.files(
  document: '/path/to/preview.json',
  audio: '/path/to/audio.ogg',
  orderingScript: '/path/to/ordering.js',
  background: '/path/to/background.avif',
)
```

`document` and `audio` are required. The optional companion script is treated
as untrusted text and never executed; only literal `n(<line>, ...)` ordering
hints are scanned. `background` is decoded by Flutter or `flutter_avif` and is
independent of metadata asset names.

The required JSON fields and a minimal valid example are documented in the
[quick start](quick_start.md#supported-document-structure).

## Player

| Parameter | Default | Description |
| --- | --- | --- |
| `source` | required | Complete preview input |
| `controller` | internally created | Playback control and state |
| `autoplay` | `true` | Starts after loading |
| `loadingBuilder` | built-in progress UI | Custom loading UI |
| `errorBuilder` | built-in error UI | Custom failure UI |
| `onLoaded` | `null` | Metadata and warning callback |

Playback drives `CustomPainter` repaint notifications without rebuilding the
widget tree for every frame. A local timestamp clock drives frames, while
audio is synchronized at load, playback, seek, and rate-change boundaries.

## Controller

Observable properties include `loadState`, `playbackState`, `metadata`,
`warnings`, `error`, `position`, `duration`, `playbackRate`, `musicVolume`,
`sfxVolume`, and `noteScale`.

```dart
await controller.play();
await controller.pause();
await controller.seek(position);
await controller.setPlaybackRate(rate);
await controller.setMusicVolume(volume);
await controller.setSfxVolume(volume);
await controller.setNoteScale(scale);
await controller.reload(source);
await controller.release();
```

One controller can be attached to one player at a time. Dispose a controller
that your application created after the player is no longer used. Playback
rates must be at least `0.05`, matching the audio backend's effective limit.

## Engine

```dart
final engine = PluvioraEngine();
try {
  final result = await engine.load(source);
  final frame = engine.render(
    position: const Duration(seconds: 10),
    width: 1920,
    height: 1080,
    songLength: result.metadata.chartDuration,
  );
  consume(frame.bytes);
} finally {
  engine.dispose();
}
```

`load(source)` initializes bundled native resources automatically. When using
the synchronous byte API directly, initialize first:

```dart
final engine = PluvioraEngine();
await engine.initialize();
final result = engine.loadBytes(documentBytes, orderingScript: scriptBytes);
```

Each engine owns an independent C++ instance. `PluvioraFrameView.bytes`
remains valid only until the next `render`, `load`, or `dispose` call on that
engine. Copy it when longer ownership is required.

## Errors and warnings

Malformed documents, ABI mismatches, invalid native calls, and missing required
inputs throw `PluvioraException` or the underlying Dart I/O exception.

Successful loads can still report warnings for recoverable compatibility cases:

- the ordering hints were absent or incomplete, so JSON traversal order was
  used for note indices;
- a BPM reference was resolved by numeric BPM matching;
- several BPM entries matched and the first was selected;
- animations without a non-null `i1` target were skipped;
- an optional background failed to decode and the fallback was used.
