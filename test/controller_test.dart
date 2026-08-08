import 'package:flutter_test/flutter_test.dart';
import 'package:pluviora/pluviora.dart';
import 'package:pluviora/src/controller.dart' show PluvioraControllerDelegate;

void main() {
  test('commits playback rates only after the delegate succeeds', () async {
    final controller = PluvioraController();
    final delegate = _ControllerDelegate();
    controller.attachDelegate(delegate);
    addTearDown(controller.dispose);

    await controller.setPlaybackRate(2.0);
    expect(controller.playbackRate, 2.0);
    expect(delegate.playbackRate, 2.0);

    delegate.failPlaybackRate = true;
    await expectLater(controller.setPlaybackRate(1.5), throwsStateError);
    expect(controller.playbackRate, 2.0);

    await expectLater(controller.setPlaybackRate(0.049), throwsArgumentError);
    expect(controller.playbackRate, 2.0);
  });

  test('loading a new source resets playback timeline state', () {
    final controller = PluvioraController();
    addTearDown(controller.dispose);

    controller.setReady(
      const PluvioraLoadResult(metadata: _metadata),
      const Duration(seconds: 30),
    );
    controller.setPlaybackState(PluvioraPlaybackState.playing);
    controller.setPosition(const Duration(seconds: 12));
    controller.setLoading();

    expect(controller.loadState, PluvioraLoadState.loading);
    expect(controller.playbackState, PluvioraPlaybackState.stopped);
    expect(controller.position, Duration.zero);
    expect(controller.duration, Duration.zero);
  });
}

const _metadata = PluvioraMetadata(
  title: 'Fixture',
  composer: 'Composer',
  illustrator: 'Illustrator',
  beatmapper: 'Beatmapper',
  difficulty: 'Test',
  audioFile: 'fixture.ogg',
  illustrationFile: 'fixture.png',
  difficultyValue: 1,
  chartDuration: Duration(seconds: 30),
  lineCount: 1,
  noteCount: 1,
  animationCount: 0,
  storyboardCount: 0,
  contentHash: 1,
);

final class _ControllerDelegate implements PluvioraControllerDelegate {
  double playbackRate = 1.0;
  bool failPlaybackRate = false;

  @override
  Future<void> setPlaybackRate(double rate) async {
    if (failPlaybackRate) throw StateError('rate failure');
    playbackRate = rate;
  }

  @override
  Future<void> pause() async {}

  @override
  Future<void> play() async {}

  @override
  Future<void> release() async {}

  @override
  Future<void> reload(PluvioraSource? source) async {}

  @override
  Future<void> seek(Duration position) async {}

  @override
  Future<void> setMusicVolume(double volume) async {}

  @override
  Future<void> setNoteScale(double scale) async {}

  @override
  Future<void> setSfxVolume(double volume) async {}
}
