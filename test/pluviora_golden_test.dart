import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:pluviora/pluviora.dart';
import 'package:pluviora/src/painter.dart';
import 'package:pluviora/src/resources.dart';

const _chart = '''
{
  "meta": {
    "Title": "Golden Fixture",
    "Composer": "Pluviora",
    "Illustrator": "Pluviora",
    "Beatmapper": "Pluviora",
    "Difficulty": "Advanced",
    "DifficultyValue": 12.5,
    "AudioFile": "fixture.ogg",
    "IllustrationFile": "fixture.png"
  },
  "bpms": [{"start": 0, "bpm": 120}],
  "lines": [
    {"notes": [
      {"bpm": 0, "startTime": 1, "endTime": 1, "type": 0,
       "isFake": false, "isAlwaysPerfect": false},
      {"bpm": 0, "startTime": 2, "endTime": 3, "type": 0,
       "isFake": false, "isAlwaysPerfect": true}
    ]}
  ],
  "storyboardObjects": [],
  "animations": [
    {"bpmId": 0, "fromBeat": 0, "toBeat": 0, "key": 12,
     "fv": "1", "tv": "1", "data": 0, "i1": 0,
     "press": 0, "ease": 0, "valueExpression": false,
     "customEaseExpression": ""},
    {"bpmId": 0, "fromBeat": 0, "toBeat": 2, "key": 0,
     "fv": "-100", "tv": "100", "data": 0, "i1": 0,
     "press": 13, "ease": 2, "valueExpression": false,
     "customEaseExpression": ""}
  ]
}
''';

void main() {
  testWidgets('renders the neutral reference frame', (tester) async {
    final chartBytes = Uint8List.fromList(utf8.encode(_chart));
    final atlas = await tester.runAsync(
      () async => PluvioraPainterResources.decode(
        await File('files/resources/default/preview_atlas.png').readAsBytes(),
        name: 'preview_atlas.png',
      ),
    );
    expect(atlas, isNotNull);
    final resources = PluvioraPainterResources(
      atlas: atlas!,
      hitRingShader: null,
      illustration: null,
      storyboards: const {},
    );
    final engine = PluvioraEngine()..loadBytes(chartBytes);
    final frames = PluvioraFrameNotifier()
      ..setResources(resources)
      ..update(
        engine.render(
          position: const Duration(milliseconds: 1500),
          width: 480,
          height: 270,
          songLength: const Duration(seconds: 4),
        ),
      );
    addTearDown(engine.dispose);
    addTearDown(frames.dispose);
    addTearDown(resources.dispose);

    const goldenKey = ValueKey('pluviora-golden');
    await tester.pumpWidget(
      Directionality(
        textDirection: TextDirection.ltr,
        child: Center(
          child: RepaintBoundary(
            key: goldenKey,
            child: SizedBox(
              width: 480,
              height: 270,
              child: CustomPaint(painter: PluvioraPainter(frames)),
            ),
          ),
        ),
      ),
    );
    await tester.pump();

    await expectLater(
      find.byKey(goldenKey),
      matchesGoldenFile('goldens/preview_frame_1_5s.png'),
    );
  }, tags: 'golden');
}
