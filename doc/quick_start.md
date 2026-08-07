# Pluviora quick start

Pluviora previews supported JSON documents with synchronized audio on Android
and iOS. Inputs may come from local paths or in-memory bytes.

## Requirements and installation

- Flutter 3.38+
- Dart 3.10.8+
- Android and iOS projects supported by the selected Flutter release
- iOS 13+

Install the latest compatible release:

```bash
flutter pub add pluviora
```

## Prepare inputs

```text
preview_bundle/
├── preview.json
├── preview.ogg
├── ordering.js       # optional; scanned but never executed
└── background.avif   # optional; PNG/JPEG/WebP also supported
```

## Supported document structure

The document root requires `meta`, `bpms`, `lines`, `animations`, and
`storyboardObjects`. Arrays may be empty when that feature is unused. A minimal
loadable document looks like this:

```json
{
  "meta": {
    "Title": "Example",
    "Composer": "Creator",
    "Illustrator": "Artist",
    "Beatmapper": "Author",
    "Difficulty": "Normal",
    "DifficultyValue": 1
  },
  "bpms": [
    {"start": 0, "bpm": 120}
  ],
  "lines": [
    {
      "notes": [
        {
          "bpm": 0,
          "startTime": 1,
          "endTime": 1,
          "type": 0,
          "isFake": false,
          "isAlwaysPerfect": false
        }
      ]
    }
  ],
  "animations": [],
  "storyboardObjects": []
}
```

`bpm` and `bpmId` normally reference entries in the `bpms` array. For archive
compatibility, an out-of-range value is matched against the numeric BPM values.
If several entries match, the first is used and a warning is returned.

Animation entries without a non-null `i1` target are skipped. All skipped
entries are summarized in one warning rather than being mapped to another
object. Optional `AudioFile` and `IllustrationFile` values inside `meta` are
metadata only; actual audio and background inputs come from `PluvioraSource`.
Malformed required fields cause a `PluvioraException` during loading.

## Add a player

```dart
import 'package:flutter/material.dart';
import 'package:pluviora/pluviora.dart';

class PreviewPage extends StatefulWidget {
  const PreviewPage({super.key});

  @override
  State<PreviewPage> createState() => _PreviewPageState();
}

class _PreviewPageState extends State<PreviewPage> {
  final controller = PluvioraController();

  late final source = PluvioraSource.files(
    document: '/path/to/preview.json',
    audio: '/path/to/preview.ogg',
    orderingScript: '/path/to/ordering.js',
    background: '/path/to/background.avif',
  );

  @override
  Widget build(BuildContext context) => Scaffold(
    backgroundColor: Colors.black,
    body: SafeArea(
      child: PluvioraPlayer(
        source: source,
        controller: controller,
        onLoaded: (result) {
          for (final warning in result.warnings) {
            debugPrint(warning.toString());
          }
        },
      ),
    ),
  );

  @override
  void dispose() {
    controller.dispose();
    super.dispose();
  }
}
```

## Static ordering hints

Some documents omit note indices that animation targets rely on. When an
optional companion script is supplied, Pluviora scans literal
`n(<line>, ...)` calls outside comments and strings to recover creation order.
No JavaScript is executed and no other expression is evaluated.

If the static sequence is absent, incomplete, or inconsistent with the JSON,
notes receive indices in line-and-note JSON traversal order. Loading continues
with a warning; affected note animation mappings may differ from the source
authoring environment.

## Use in-memory inputs

```dart
final source = PluvioraSource(
  document: PluvioraAsset.memory(documentBytes, name: 'preview.json'),
  audio: PluvioraAsset.memory(audioBytes, name: 'preview.ogg'),
  orderingScript: PluvioraAsset.memory(orderingBytes, name: 'ordering.js'),
  background: PluvioraAsset.memory(backgroundBytes, name: 'cover.avif'),
);
```

The caller owns downloading, permissions, and file selection. The package does
not request storage permissions.

## Playback control

Wait for `onLoaded`, or for
`controller.loadState == PluvioraLoadState.ready`, before invoking controls.

```dart
await controller.pause();
await controller.seek(const Duration(seconds: 30));
await controller.setPlaybackRate(1.25);
await controller.setMusicVolume(0.8);
await controller.setSfxVolume(0.6);
await controller.setNoteScale(1.1);
await controller.play();
```

Use `controller.reload(nextSource)` to replace the current input bundle.

## Backgrounds and storyboard entries

The optional `background` input supports AVIF, PNG, JPEG, and formats handled
by Flutter's image decoder. A decode failure falls back to the built-in
background and returns a warning.

Text storyboard entries are rendered in their requested layer. Picture
storyboard entries are intentionally skipped to preserve reference-renderer
behavior; no separate picture-asset map is accepted by the public API.

## Troubleshooting

- Controller is not connected: mount its `PluvioraPlayer` first.
- Player is not ready: wait for `onLoaded` or the `ready` load state.
- Audio cannot be decoded: verify platform support for the input format.
- Ordering file is missing: rendering continues with JSON traversal order.
- Animations were skipped: inspect warnings for missing `i1` targets.

See the [API guide](api.md) and [architecture notes](architecture.md).
