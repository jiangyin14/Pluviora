import 'package:flutter/foundation.dart';

import 'models.dart';
import 'source.dart';

abstract interface class PluvioraControllerDelegate {
  Future<void> play();
  Future<void> pause();
  Future<void> seek(Duration position);
  Future<void> setPlaybackRate(double rate);
  Future<void> setMusicVolume(double volume);
  Future<void> setSfxVolume(double volume);
  Future<void> setNoteScale(double scale);
  Future<void> reload(PluvioraSource? source);
  Future<void> release();
}

/// Controls a [PluvioraPlayer] without rebuilding it on every frame.
final class PluvioraController extends ChangeNotifier {
  PluvioraLoadState _loadState = PluvioraLoadState.idle;
  PluvioraPlaybackState _playbackState = PluvioraPlaybackState.stopped;
  PluvioraMetadata? _metadata;
  List<PluvioraWarning> _warnings = const [];
  Object? _error;
  Duration _position = Duration.zero;
  Duration _duration = Duration.zero;
  double _playbackRate = 1.0;
  double _musicVolume = 1.0;
  double _sfxVolume = 1.0;
  double _noteScale = 1.0;
  PluvioraControllerDelegate? _delegate;
  bool _disposed = false;

  PluvioraLoadState get loadState => _loadState;
  PluvioraPlaybackState get playbackState => _playbackState;
  PluvioraMetadata? get metadata => _metadata;
  List<PluvioraWarning> get warnings => _warnings;
  Object? get error => _error;
  Duration get position => _position;
  Duration get duration => _duration;
  double get playbackRate => _playbackRate;
  double get musicVolume => _musicVolume;
  double get sfxVolume => _sfxVolume;
  double get noteScale => _noteScale;

  Future<void> play() => _requireDelegate().play();
  Future<void> pause() => _requireDelegate().pause();
  Future<void> seek(Duration position) => _requireDelegate().seek(position);

  Future<void> setPlaybackRate(double rate) async {
    if (!rate.isFinite || rate < 0.05) {
      throw ArgumentError.value(rate, 'rate', 'Must be at least 0.05.');
    }
    await _requireDelegate().setPlaybackRate(rate);
    _playbackRate = rate;
    _notify();
  }

  Future<void> setMusicVolume(double volume) async {
    _musicVolume = volume.clamp(0.0, 1.0);
    await _requireDelegate().setMusicVolume(_musicVolume);
    _notify();
  }

  Future<void> setSfxVolume(double volume) async {
    _sfxVolume = volume.clamp(0.0, 1.0);
    await _requireDelegate().setSfxVolume(_sfxVolume);
    _notify();
  }

  Future<void> setNoteScale(double scale) async {
    if (!scale.isFinite || scale <= 0) {
      throw ArgumentError.value(scale, 'scale', 'Must be greater than zero.');
    }
    _noteScale = scale;
    await _requireDelegate().setNoteScale(scale);
    _notify();
  }

  Future<void> reload([PluvioraSource? source]) =>
      _requireDelegate().reload(source);

  Future<void> release() => _requireDelegate().release();

  @internal
  void attachDelegate(PluvioraControllerDelegate delegate) {
    if (_disposed) throw StateError('PluvioraController has been disposed.');
    if (_delegate != null && !identical(_delegate, delegate)) {
      throw StateError(
        'A PluvioraController cannot control multiple players at once.',
      );
    }
    _delegate = delegate;
  }

  @internal
  void detachDelegate(PluvioraControllerDelegate delegate) {
    if (identical(_delegate, delegate)) _delegate = null;
  }

  @internal
  void setLoading() {
    _loadState = PluvioraLoadState.loading;
    _playbackState = PluvioraPlaybackState.stopped;
    _position = Duration.zero;
    _duration = Duration.zero;
    _error = null;
    _notify();
  }

  @internal
  void setReady(PluvioraLoadResult result, Duration duration) {
    _loadState = PluvioraLoadState.ready;
    _metadata = result.metadata;
    _warnings = result.warnings;
    _duration = duration;
    _error = null;
    _notify();
  }

  @internal
  void setFailure(Object error) {
    _loadState = PluvioraLoadState.failed;
    _playbackState = PluvioraPlaybackState.stopped;
    _error = error;
    _notify();
  }

  @internal
  void setPlaybackState(PluvioraPlaybackState state) {
    if (_playbackState == state) return;
    _playbackState = state;
    _notify();
  }

  @internal
  void setPosition(Duration position) {
    _position = position;
  }

  @internal
  void setWarnings(List<PluvioraWarning> warnings) {
    _warnings = List.unmodifiable(warnings);
    _notify();
  }

  PluvioraControllerDelegate _requireDelegate() {
    if (_disposed) throw StateError('PluvioraController has been disposed.');
    return _delegate ??
        (throw StateError('PluvioraController is not attached to a player.'));
  }

  void _notify() {
    if (!_disposed) notifyListeners();
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _delegate = null;
    _loadState = PluvioraLoadState.disposed;
    super.dispose();
  }
}
