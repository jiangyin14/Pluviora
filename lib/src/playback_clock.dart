typedef PluvioraTimestamp = DateTime Function();
typedef PluvioraElapsedTimestamp = Duration Function();

/// Lightweight local playback clock used by the player render loop.
///
/// Audio synchronization happens explicitly at player state boundaries. Frame
/// rendering reads [DateTime.timestamp] through [position], with a monotonic
/// elapsed-time source guarding against system clock corrections.
final class PluvioraPlaybackClock {
  PluvioraPlaybackClock({
    PluvioraTimestamp? timestamp,
    PluvioraElapsedTimestamp? elapsedTimestamp,
  }) : _timestamp = timestamp ?? DateTime.timestamp,
       _elapsedTimestamp = elapsedTimestamp ?? _createElapsedTimestamp() {
    _anchorTimestamp = _timestamp();
    _anchorElapsedTimestamp = _elapsedTimestamp();
  }

  final PluvioraTimestamp _timestamp;
  final PluvioraElapsedTimestamp _elapsedTimestamp;
  late DateTime _anchorTimestamp;
  late Duration _anchorElapsedTimestamp;
  Duration _anchorPosition = Duration.zero;
  Duration _lastPosition = Duration.zero;
  Duration _duration = Duration.zero;
  double _rate = 1.0;
  bool _running = false;

  static const int _wallClockToleranceMicroseconds = 250000;

  Duration get position => _positionAt(
    timestamp: _timestamp(),
    elapsedTimestamp: _elapsedTimestamp(),
  );

  void reset({
    required Duration duration,
    required double rate,
    required bool running,
  }) {
    _validateRate(rate);
    _duration = duration.isNegative ? Duration.zero : duration;
    _rate = rate;
    _running = running;
    _anchorPosition = Duration.zero;
    _lastPosition = Duration.zero;
    _anchorTimestamp = _timestamp();
    _anchorElapsedTimestamp = _elapsedTimestamp();
  }

  Duration synchronize(
    Duration position, {
    required bool running,
    double? rate,
  }) {
    final nextRate = rate ?? _rate;
    _validateRate(nextRate);
    _anchorPosition = _clamp(position);
    _lastPosition = _anchorPosition;
    _anchorTimestamp = _timestamp();
    _anchorElapsedTimestamp = _elapsedTimestamp();
    _rate = nextRate;
    _running = running;
    return _anchorPosition;
  }

  Duration _positionAt({
    required DateTime timestamp,
    required Duration elapsedTimestamp,
  }) {
    if (!_running) return _anchorPosition;
    final wallElapsed = timestamp.difference(_anchorTimestamp).inMicroseconds;
    final monotonicElapsed =
        (elapsedTimestamp - _anchorElapsedTimestamp).inMicroseconds;
    final wallClockDrift = (wallElapsed - monotonicElapsed).abs();
    final elapsed =
        wallElapsed < 0 ||
            monotonicElapsed < 0 ||
            wallClockDrift > _wallClockToleranceMicroseconds
        ? monotonicElapsed
        : wallElapsed;
    if (elapsed <= 0) {
      _anchorPosition = _lastPosition;
      _anchorTimestamp = timestamp;
      _anchorElapsedTimestamp = elapsedTimestamp;
      return _lastPosition;
    }
    final nextPosition = _clamp(
      Duration(
        microseconds:
            _anchorPosition.inMicroseconds + (elapsed * _rate).round(),
      ),
    );
    if (nextPosition < _lastPosition) {
      _anchorPosition = _lastPosition;
      _anchorTimestamp = timestamp;
      _anchorElapsedTimestamp = elapsedTimestamp;
      return _lastPosition;
    }
    _lastPosition = nextPosition;
    return nextPosition;
  }

  Duration _clamp(Duration position) {
    var microseconds = position.inMicroseconds;
    if (microseconds < 0) microseconds = 0;
    if (_duration > Duration.zero && microseconds > _duration.inMicroseconds) {
      microseconds = _duration.inMicroseconds;
    }
    return Duration(microseconds: microseconds);
  }

  static void _validateRate(double rate) {
    if (!rate.isFinite || rate <= 0) {
      throw ArgumentError.value(rate, 'rate', 'Must be greater than zero.');
    }
  }

  static PluvioraElapsedTimestamp _createElapsedTimestamp() {
    final stopwatch = Stopwatch()..start();
    return () => stopwatch.elapsed;
  }
}
