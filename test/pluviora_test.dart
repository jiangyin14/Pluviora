import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:pluviora/pluviora.dart';

const _chart = '''
{
  "meta": {
    "Title": "Fixture",
    "Composer": "Composer",
    "Illustrator": "Illustrator",
    "Beatmapper": "Beatmapper",
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
  "storyboardObjects": [
    {"type": 1, "data": "Fixture text", "layer": 2}
  ],
  "animations": [
    {"bpmId": 0, "fromBeat": 0, "toBeat": 0, "key": 12,
     "fv": "1", "tv": "1", "data": 0, "i1": 0,
     "press": 0, "ease": 0, "valueExpression": false,
     "customEaseExpression": ""},
    {"bpmId": 0, "fromBeat": 0, "toBeat": 2, "key": 0,
     "fv": "-400", "tv": "400", "data": 0, "i1": 0,
     "press": 13, "ease": 2, "valueExpression": false,
     "customEaseExpression": ""}
  ]
}
''';

Uint8List get _bytes => Uint8List.fromList(utf8.encode(_chart));

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('loads runtime JSON and exposes metadata', () async {
    final engine = PluvioraEngine();
    addTearDown(engine.dispose);
    await engine.initialize();
    final result = engine.loadBytes(
      _bytes,
      orderingScript: Uint8List.fromList(utf8.encode('n(0, 0); n(0, 1);')),
    );
    expect(result.metadata.title, 'Fixture');
    expect(result.metadata.noteCount, 2);
    expect(result.metadata.animationCount, 2);
    expect(result.metadata.storyboardCount, 1);
    expect(result.metadata.difficultyValue, 12.5);
  });

  test('renders a dynamic, aligned native command view', () async {
    final engine = PluvioraEngine();
    addTearDown(engine.dispose);
    await engine.initialize();
    engine.loadBytes(_bytes);
    final frame = engine.render(
      position: const Duration(milliseconds: 1500),
      width: 1920,
      height: 1080,
      songLength: const Duration(seconds: 4),
    );
    expect(frame.commandCount, greaterThan(8));
    expect(frame.bytes.lengthInBytes % 4, 0);
    expect(frame.bytes.buffer, isNotNull);

    final data = ByteData.sublistView(frame.bytes);
    var offset = 0;
    var foundLineHead = false;
    while (offset + 8 <= data.lengthInBytes) {
      final type = data.getUint16(offset, Endian.little);
      final length = data.getUint32(offset + 4, Endian.little);
      if (type == 3 && data.getUint32(offset + 32, Endian.little) == 0) {
        expect(
          data.getFloat32(offset + 8, Endian.little),
          closeTo(1266.25, 1e-4),
        );
        expect(data.getFloat32(offset + 12, Endian.little), closeTo(890, 1e-4));
        foundLineHead = true;
        break;
      }
      offset = (offset + length + 3) & ~3;
    }
    expect(foundLineHead, isTrue);
  });

  test(
    'supports reverse seek, multiple instances, and repeated Dart release',
    () async {
      final first = PluvioraEngine();
      final second = PluvioraEngine();
      await Future.wait([first.initialize(), second.initialize()]);
      first.loadBytes(_bytes);
      second.loadBytes(_bytes);
      first.render(
        position: const Duration(milliseconds: 500),
        width: 800,
        height: 600,
      );
      first.seek(const Duration(milliseconds: 2250));
      final sought = first.render(
        position: const Duration(milliseconds: 2250),
        width: 800,
        height: 600,
      );
      expect(sought.hitCount, 0);
      first.seek(const Duration(seconds: 1));
      final exact = first.render(
        position: const Duration(seconds: 1),
        width: 800,
        height: 600,
      );
      expect(exact.hitCount, 0);
      first.seek(const Duration(milliseconds: 250));
      final reversed = first.render(
        position: const Duration(milliseconds: 250),
        width: 800,
        height: 600,
      );
      final replayed = first.render(
        position: const Duration(seconds: 1),
        width: 800,
        height: 600,
      );
      final other = second.render(
        position: const Duration(seconds: 1),
        width: 1280,
        height: 720,
      );
      expect(reversed.commandCount, greaterThan(0));
      expect(replayed.hitCount, 1);
      expect(other.commandCount, greaterThan(0));
      first.dispose();
      first.dispose();
      second.dispose();
      second.dispose();
    },
  );

  test('turns malformed input into PluvioraException', () async {
    final engine = PluvioraEngine();
    addTearDown(engine.dispose);
    await engine.initialize();
    expect(
      () => engine.loadBytes(Uint8List.fromList(utf8.encode('{broken'))),
      throwsA(isA<PluvioraException>()),
    );
  });
}
