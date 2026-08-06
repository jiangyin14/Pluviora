# Pluviora quick start

Pluviora previews supported JSON documents with synchronized audio on Android
and iOS. Inputs may come from local paths or in-memory bytes.

## Requirements and installation

- Flutter 3.38+
- Dart 3.10.8+
- Android API 24+
- iOS 13+

Install the latest compatible release:

```bash
flutter pub add pluviora
```

Or add `pluviora: ^0.2.0` under `dependencies` in `pubspec.yaml` and run
`flutter pub get`.

## Prepare inputs

```text
preview_bundle/
├── preview.json
├── preview.ogg
├── ordering.js       # optional; scanned but never executed
├── background.avif   # optional
└── overlays/         # optional named images
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

`bpm` and `bpmId` values reference entries in the `bpms` array. Optional
`AudioFile` and `IllustrationFile` values inside `meta` are metadata only; the
actual audio and background inputs are supplied through `PluvioraSource`.
Malformed or unsupported fields cause a `PluvioraException` during loading.

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
    overlayAssets: {
      'overlay.png': '/path/to/overlays/overlay.png',
    },
  );

  @override
  Widget build(BuildContext context) => Scaffold(
    backgroundColor: Colors.black,
    body: SafeArea(
      child: PluvioraPlayer(
        source: source,
        controller: controller,
        onLoaded: (result) {
          debugPrint('Loaded ${result.metadata.title}');
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

## Optional images

Named overlay images are matched using identifiers stored in the preview
document. Missing images are skipped and returned as non-fatal warnings.

## Troubleshooting

- Controller is not connected: mount its `PluvioraPlayer` first.
- Player is not ready: wait for `onLoaded` or the `ready` load state.
- Audio cannot be decoded: verify platform support for the input format.
- Ordering file is missing: rendering continues without its static hints.
- Optional image is missing: verify its map key matches the document identifier.

See the [API guide](api.md) and [architecture notes](architecture.md).
