# Pluviora API guide

## Main types

| Type | Purpose |
| --- | --- |
| `PluvioraAsset` | A path-backed or memory-backed input |
| `PluvioraSource` | Document, audio, and optional companion resources |
| `PluvioraPlayer` | A self-contained preview Widget |
| `PluvioraController` | Playback control and observable state |
| `PluvioraEngine` | Low-level native engine without audio or Widgets |
| `PluvioraLoadResult` | Metadata and non-fatal warnings |
| `PluvioraFrameView` | A zero-copy native drawing-command view |

## Source

```dart
PluvioraSource.files(
  document: '/path/to/preview.json',
  audio: '/path/to/audio.ogg',
  orderingScript: '/path/to/ordering.js',
  background: '/path/to/background.avif',
  overlayAssets: {
    'overlay.png': '/path/to/overlay.png',
  },
)
```

The optional companion script is treated as untrusted text and never executed.
The required JSON root fields and a minimal valid example are documented in the
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
Widget tree for every frame.

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

One controller can be attached to one player at a time.

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

Each engine owns an independent C++ instance. `PluvioraFrameView.bytes`
remains valid only until the next `render`, `load`, or `dispose` call on that
engine. Copy it when longer ownership is required.

## Errors and warnings

Malformed documents, ABI mismatches, and invalid native calls throw
`PluvioraException`. Missing optional images and decode failures are reported
as `PluvioraWarning` values so playback can continue.
