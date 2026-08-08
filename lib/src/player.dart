import 'dart:async';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'controller.dart';
import 'engine.dart';
import 'models.dart';
import 'painter.dart';
import 'playback_clock.dart';
import 'resources.dart';
import 'source.dart';

typedef PluvioraErrorBuilder =
    Widget Function(BuildContext context, Object error);

/// An automatically rendered and audio-synchronized special-file preview.
final class PluvioraPlayer extends StatefulWidget {
  const PluvioraPlayer({
    super.key,
    required this.source,
    this.controller,
    this.autoplay = true,
    this.loadingBuilder,
    this.errorBuilder,
    this.onLoaded,
  });

  final PluvioraSource source;
  final PluvioraController? controller;
  final bool autoplay;
  final WidgetBuilder? loadingBuilder;
  final PluvioraErrorBuilder? errorBuilder;
  final ValueChanged<PluvioraLoadResult>? onLoaded;

  @override
  State<PluvioraPlayer> createState() => _PluvioraPlayerState();
}

final class _PluvioraPlayerState extends State<PluvioraPlayer>
    with SingleTickerProviderStateMixin, WidgetsBindingObserver
    implements PluvioraControllerDelegate {
  late final PluvioraEngine _engine;
  late final Ticker _ticker;
  late PluvioraController _controller;
  late bool _ownsController;
  final PluvioraFrameNotifier _frames = PluvioraFrameNotifier();
  final PluvioraPlaybackClock _clock = PluvioraPlaybackClock();

  PluvioraSource? _source;
  PluvioraPainterResources? _resources;
  AudioSource? _musicSource;
  SoundHandle? _musicHandle;
  AudioSource? _hitSource;
  AudioSource? _dragSource;
  ui.Size _canvasSize = ui.Size.zero;
  Duration _duration = Duration.zero;
  bool _ready = false;
  bool _released = false;
  bool _needsRender = true;
  int _loadToken = 0;
  PluvioraSource? _scheduledLoadSource;
  Completer<void>? _scheduledLoadCompleter;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _engine = PluvioraEngine();
    _ticker = createTicker(_tick);
    _attachController(widget.controller);
    unawaited(_loadOutsideBuild(widget.source));
  }

  @override
  void didUpdateWidget(covariant PluvioraPlayer oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (!identical(oldWidget.controller, widget.controller)) {
      _controller.detachDelegate(this);
      if (_ownsController) _controller.dispose();
      _attachController(widget.controller);
    }
    if (!identical(oldWidget.source, widget.source)) {
      unawaited(_loadOutsideBuild(widget.source));
    }
  }

  void _attachController(PluvioraController? value) {
    _ownsController = value == null;
    _controller = value ?? PluvioraController();
    _controller.attachDelegate(this);
  }

  Future<void> _loadOutsideBuild(PluvioraSource source) {
    if (_released) {
      return Future.error(StateError('PluvioraPlayer has been released.'));
    }
    if (SchedulerBinding.instance.schedulerPhase !=
        SchedulerPhase.persistentCallbacks) {
      return _load(source);
    }

    _scheduledLoadSource = source;
    final pending = _scheduledLoadCompleter;
    if (pending != null) return pending.future;

    final completer = Completer<void>();
    _scheduledLoadCompleter = completer;
    WidgetsBinding.instance.addPostFrameCallback((_) async {
      final scheduledSource = _scheduledLoadSource;
      if (identical(_scheduledLoadCompleter, completer)) {
        _scheduledLoadCompleter = null;
        _scheduledLoadSource = null;
      }
      if (!mounted || _released || scheduledSource == null) {
        if (!completer.isCompleted) completer.complete();
        return;
      }
      try {
        await _load(scheduledSource);
        if (!completer.isCompleted) completer.complete();
      } catch (error, stackTrace) {
        if (!completer.isCompleted) {
          completer.completeError(error, stackTrace);
        }
      }
    });
    return completer.future;
  }

  Future<void> _load(PluvioraSource source) async {
    final token = ++_loadToken;
    _controller.setLoading();
    _ready = false;
    _needsRender = true;
    try {
      await _disposeLoadedState();
      final documentBytes = await source.document.read();
      final orderingScript = source.orderingScript == null
          ? null
          : await source.orderingScript!.read();
      if (!_isCurrent(token)) return;
      await _engine.initialize();
      if (!_isCurrent(token)) return;
      final loadResult = _engine.loadBytes(
        documentBytes,
        orderingScript: orderingScript,
      );

      final audio = await _PluvioraAudio.ensureInitialized();
      final results = await Future.wait<Object>([
        PluvioraPainterResources.load(source),
        _loadMusic(audio, source),
        _PluvioraAudio.hitSounds(),
      ]);
      final resourceLoad = results[0] as PluvioraResourceLoad;
      final loadedMusic = results[1] as AudioSource;
      if (!_isCurrent(token)) {
        resourceLoad.resources.dispose();
        await audio.disposeSource(loadedMusic);
        return;
      }

      _resources = resourceLoad.resources;
      _frames.setResources(resourceLoad.resources);
      _musicSource = loadedMusic;
      final hitSounds = results[2] as (AudioSource, AudioSource);
      _hitSource = hitSounds.$1;
      _dragSource = hitSounds.$2;
      _duration = audio.getLength(_musicSource!);
      _musicHandle = audio.play(
        _musicSource!,
        paused: true,
        volume: _controller.musicVolume,
      );
      audio.setRelativePlaySpeed(_musicHandle!, _controller.playbackRate);
      audio.seek(_musicHandle!, Duration.zero);
      _engine
        ..seek(Duration.zero)
        ..setNoteScale(_controller.noteScale)
        ..setFlowSpeed(1.66);
      _clock.reset(
        duration: _duration,
        rate: _controller.playbackRate,
        running: false,
      );
      _controller.setPosition(Duration.zero);

      final finalResult = PluvioraLoadResult(
        metadata: loadResult.metadata,
        warnings: List.unmodifiable([
          ...loadResult.warnings,
          ...resourceLoad.warnings,
        ]),
      );
      if (widget.autoplay) {
        audio.setPause(_musicHandle!, false);
        _clock.synchronize(
          Duration.zero,
          running: true,
          rate: _controller.playbackRate,
        );
      }
      _source = source;
      _ready = true;
      _controller.setReady(finalResult, _duration);
      _controller.setPlaybackState(
        widget.autoplay
            ? PluvioraPlaybackState.playing
            : PluvioraPlaybackState.paused,
      );
      widget.onLoaded?.call(finalResult);
      if (!_ticker.isActive) _ticker.start();
      if (mounted) setState(() {});
    } catch (error) {
      if (!_isCurrent(token)) return;
      await _disposeLoadedState();
      if (!_isCurrent(token)) return;
      _controller.setFailure(error);
      if (mounted) setState(() {});
    }
  }

  bool _isCurrent(int token) => mounted && !_released && token == _loadToken;

  Future<AudioSource> _loadMusic(SoLoud audio, PluvioraSource source) {
    final music = source.audio;
    if (music.path != null) {
      return audio.loadFile(music.path!, mode: LoadMode.memory);
    }
    return music.read().then(
      (bytes) => audio.loadMem(music.name, bytes, mode: LoadMode.memory),
    );
  }

  void _tick(Duration _) {
    if (!_ready || _canvasSize.isEmpty || _musicHandle == null) return;
    final state = _controller.playbackState;
    if (state != PluvioraPlaybackState.playing && !_needsRender) return;
    try {
      final position = _clock.position;
      _controller.setPosition(position);
      final frame = _engine.render(
        position: position,
        width: _canvasSize.width,
        height: _canvasSize.height,
        songLength: _duration,
      );
      _frames.update(frame);
      _needsRender = false;
      if (state == PluvioraPlaybackState.playing) {
        _playHitSounds(frame);
        if (_duration > Duration.zero && position >= _duration) {
          _clock.synchronize(_duration, running: false);
          _controller.setPosition(_duration);
          _controller.setPlaybackState(PluvioraPlaybackState.completed);
        }
      }
    } catch (error) {
      _controller.setFailure(error);
    }
  }

  void _playHitSounds(PluvioraFrameView frame) {
    final audio = SoLoud.instance;
    final hit = _hitSource;
    final drag = _dragSource;
    if (hit != null) {
      for (var i = 0; i < frame.hitCount; i++) {
        audio.play(hit, volume: _controller.sfxVolume);
      }
    }
    if (drag != null) {
      final count = frame.dragCount + frame.fractureCount;
      for (var i = 0; i < count; i++) {
        audio.play(drag, volume: _controller.sfxVolume);
      }
    }
  }

  @override
  Future<void> play() async {
    _ensureReady();
    final state = _controller.playbackState;
    if (state == PluvioraPlaybackState.completed) {
      _seekTo(Duration.zero);
    }
    final position = _clock.position;
    final audio = SoLoud.instance;
    final handle = _ensureMusicHandle(audio).handle;
    audio.seek(handle, position);
    audio.setRelativePlaySpeed(handle, _controller.playbackRate);
    audio.setPause(handle, false);
    _clock.synchronize(position, running: true, rate: _controller.playbackRate);
    _controller.setPosition(position);
    _controller.setPlaybackState(PluvioraPlaybackState.playing);
  }

  @override
  Future<void> pause() async {
    _ensureReady();
    if (_controller.playbackState != PluvioraPlaybackState.playing) return;
    final position = _clock.position;
    final audio = SoLoud.instance;
    final handle = _ensureMusicHandle(audio).handle;
    audio.setPause(handle, true);
    audio.seek(handle, position);
    _clock.synchronize(position, running: false);
    _controller.setPosition(position);
    _controller.setPlaybackState(PluvioraPlaybackState.paused);
    _needsRender = true;
  }

  @override
  Future<void> seek(Duration position) async {
    _ensureReady();
    _seekTo(position);
  }

  void _seekTo(Duration position) {
    final clamped = Duration(
      microseconds: position.inMicroseconds.clamp(0, _duration.inMicroseconds),
    );
    final audio = SoLoud.instance;
    final state = _controller.playbackState;
    final ensuredHandle = _ensureMusicHandle(audio);
    final handle = ensuredHandle.handle;
    audio.seek(handle, clamped);
    if (ensuredHandle.recreated && state == PluvioraPlaybackState.playing) {
      audio.setPause(handle, false);
    }
    _engine.seek(clamped);
    _clock.synchronize(
      clamped,
      running: state == PluvioraPlaybackState.playing,
      rate: _controller.playbackRate,
    );
    _controller.setPosition(clamped);
    if (state == PluvioraPlaybackState.completed && clamped < _duration) {
      _controller.setPlaybackState(PluvioraPlaybackState.paused);
    }
    _needsRender = true;
  }

  @override
  Future<void> setPlaybackRate(double rate) async {
    _ensureReady();
    final position = _clock.position;
    if (_controller.playbackState == PluvioraPlaybackState.completed) {
      _clock.synchronize(position, running: false, rate: rate);
      _controller.setPosition(position);
      return;
    }
    final audio = SoLoud.instance;
    final state = _controller.playbackState;
    final ensuredHandle = _ensureMusicHandle(audio);
    final handle = ensuredHandle.handle;
    audio.seek(handle, position);
    audio.setRelativePlaySpeed(handle, rate);
    if (ensuredHandle.recreated && state == PluvioraPlaybackState.playing) {
      audio.setPause(handle, false);
    }
    _clock.synchronize(
      position,
      running: state == PluvioraPlaybackState.playing,
      rate: rate,
    );
    _controller.setPosition(position);
  }

  @override
  Future<void> setMusicVolume(double volume) async {
    _ensureReady();
    final audio = SoLoud.instance;
    final handle = _musicHandle;
    if (handle != null && audio.getIsValidVoiceHandle(handle)) {
      audio.setVolume(handle, volume);
    }
  }

  @override
  Future<void> setSfxVolume(double volume) async {}

  @override
  Future<void> setNoteScale(double scale) async {
    _ensureReady();
    _engine.setNoteScale(scale);
    _needsRender = true;
  }

  @override
  Future<void> reload(PluvioraSource? source) async {
    if (_released) throw StateError('PluvioraPlayer has been released.');
    await _loadOutsideBuild(source ?? _source ?? widget.source);
  }

  @override
  Future<void> release() async {
    if (_released) return;
    _released = true;
    _loadToken++;
    _scheduledLoadSource = null;
    final scheduledLoad = _scheduledLoadCompleter;
    _scheduledLoadCompleter = null;
    if (scheduledLoad != null && !scheduledLoad.isCompleted) {
      scheduledLoad.complete();
    }
    _ticker.stop();
    await _disposeLoadedState();
    _engine.dispose();
    _controller.detachDelegate(this);
  }

  Future<void> _disposeLoadedState() async {
    final audio = SoLoud.instance;
    final handle = _musicHandle;
    final source = _musicSource;
    final resources = _resources;
    _musicHandle = null;
    _musicSource = null;
    _resources = null;
    _duration = Duration.zero;
    _ready = false;
    _clock.reset(
      duration: Duration.zero,
      rate: _controller.playbackRate,
      running: false,
    );
    _controller.setPosition(Duration.zero);
    if (handle != null && audio.isInitialized) {
      try {
        await audio.stop(handle);
      } catch (_) {}
    }
    if (source != null && audio.isInitialized) {
      try {
        await audio.disposeSource(source);
      } catch (_) {}
    }
    resources?.dispose();
  }

  ({SoundHandle handle, bool recreated}) _ensureMusicHandle(SoLoud audio) {
    final current = _musicHandle;
    if (current != null && audio.getIsValidVoiceHandle(current)) {
      return (handle: current, recreated: false);
    }
    final source = _musicSource;
    if (source == null) {
      throw StateError('PluvioraPlayer has no loaded music source.');
    }
    final handle = audio.play(
      source,
      paused: true,
      volume: _controller.musicVolume,
    );
    audio.setRelativePlaySpeed(handle, _controller.playbackRate);
    _musicHandle = handle;
    return (handle: handle, recreated: true);
  }

  void _ensureReady() {
    if (!_ready || _musicHandle == null) {
      throw StateError('PluvioraPlayer has not finished loading.');
    }
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state != AppLifecycleState.resumed &&
        _ready &&
        _controller.playbackState == PluvioraPlaybackState.playing) {
      unawaited(pause());
    }
  }

  @override
  Widget build(BuildContext context) {
    final error = _controller.error;
    if (error != null) {
      return widget.errorBuilder?.call(context, error) ??
          ColoredBox(
            color: const Color(0xff091124),
            child: Center(
              child: Text(
                'Pluviora failed to load\n$error',
                textAlign: TextAlign.center,
                style: const TextStyle(color: Colors.white),
              ),
            ),
          );
    }
    if (!_ready) {
      return widget.loadingBuilder?.call(context) ??
          const ColoredBox(
            color: Color(0xff091124),
            child: Center(child: CircularProgressIndicator()),
          );
    }
    return LayoutBuilder(
      builder: (context, constraints) {
        _canvasSize = constraints.biggest;
        _needsRender = true;
        return RepaintBoundary(
          child: CustomPaint(
            painter: PluvioraPainter(_frames),
            size: constraints.biggest,
          ),
        );
      },
    );
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    unawaited(release());
    _ticker.dispose();
    _frames.dispose();
    if (_ownsController) _controller.dispose();
    super.dispose();
  }
}

final class _PluvioraAudio {
  static Future<SoLoud>? _initializing;
  static Future<(AudioSource, AudioSource)>? _hitSounds;

  static Future<SoLoud> ensureInitialized() => _initializing ??= _initialize();

  static Future<SoLoud> _initialize() async {
    final audio = SoLoud.instance;
    if (!audio.isInitialized) {
      await audio.init(sampleRate: 48000, bufferSize: 1024);
    }
    return audio;
  }

  static Future<(AudioSource, AudioSource)> hitSounds() =>
      _hitSounds ??= _loadHitSounds();

  static Future<(AudioSource, AudioSource)> _loadHitSounds() async {
    final audio = await ensureInitialized();
    Future<Uint8List> asset(String path) async =>
        (await rootBundle.load(path)).buffer.asUint8List();
    final bytes = await Future.wait([
      asset('packages/pluviora/files/resources/runtime/sounds/primary.ogg'),
      asset('packages/pluviora/files/resources/runtime/sounds/secondary.ogg'),
    ]);
    return (
      await audio.loadMem('pluviora_primary.ogg', bytes[0]),
      await audio.loadMem('pluviora_secondary.ogg', bytes[1]),
    );
  }
}
