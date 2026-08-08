import 'package:flutter_test/flutter_test.dart';
import 'package:pluviora/src/playback_clock.dart';

void main() {
  test('advances from DateTime timestamps and honors playback boundaries', () {
    var now = DateTime.utc(2026, 8, 8);
    var elapsed = Duration.zero;
    final clock = PluvioraPlaybackClock(
      timestamp: () => now,
      elapsedTimestamp: () => elapsed,
    );

    clock.reset(
      duration: const Duration(seconds: 10),
      rate: 1.0,
      running: true,
    );
    now = now.add(const Duration(milliseconds: 1500));
    elapsed += const Duration(milliseconds: 1500);
    expect(clock.position, const Duration(milliseconds: 1500));

    clock.synchronize(const Duration(seconds: 4), running: true);
    now = now.add(const Duration(seconds: 1));
    elapsed += const Duration(seconds: 1);
    expect(clock.position, const Duration(seconds: 5));

    final paused = clock.synchronize(clock.position, running: false);
    now = now.add(const Duration(seconds: 3));
    elapsed += const Duration(seconds: 3);
    expect(clock.position, paused);

    clock.synchronize(paused, running: true, rate: 2.0);
    now = now.add(const Duration(milliseconds: 750));
    elapsed += const Duration(milliseconds: 750);
    expect(clock.position, const Duration(milliseconds: 6500));
  });

  test('clamps bounded timelines and supports unknown durations', () {
    var now = DateTime.utc(2026, 8, 8);
    var elapsed = Duration.zero;
    final clock = PluvioraPlaybackClock(
      timestamp: () => now,
      elapsedTimestamp: () => elapsed,
    );

    clock.reset(duration: const Duration(seconds: 2), rate: 1.0, running: true);
    now = now.add(const Duration(seconds: 3));
    elapsed += const Duration(seconds: 3);
    expect(clock.position, const Duration(seconds: 2));
    expect(
      clock.synchronize(const Duration(seconds: -1), running: false),
      Duration.zero,
    );

    clock.reset(duration: Duration.zero, rate: 1.0, running: true);
    now = now.add(const Duration(seconds: 3));
    elapsed += const Duration(seconds: 3);
    expect(clock.position, const Duration(seconds: 3));
  });

  test('ignores backward and forward wall-clock corrections', () {
    var now = DateTime.utc(2026, 8, 8, 12);
    var elapsed = Duration.zero;
    final clock = PluvioraPlaybackClock(
      timestamp: () => now,
      elapsedTimestamp: () => elapsed,
    );
    clock.reset(
      duration: const Duration(seconds: 10),
      rate: 1.0,
      running: true,
    );

    now = now.add(const Duration(seconds: 2));
    elapsed += const Duration(seconds: 2);
    expect(clock.position, const Duration(seconds: 2));
    now = now.subtract(const Duration(seconds: 3));
    elapsed += const Duration(seconds: 1);
    expect(clock.position, const Duration(seconds: 3));

    clock.synchronize(clock.position, running: true);
    now = now.add(const Duration(minutes: 5));
    elapsed += const Duration(seconds: 1);
    expect(clock.position, const Duration(seconds: 4));
  });
}
