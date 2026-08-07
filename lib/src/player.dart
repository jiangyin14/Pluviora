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

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _engine = PluvioraEngine();
    _ticker = createTicker(_tick);
    _attachController(widget.controller);
    unawaited(_load(widget.source));
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
      unawaited(reload(widget.source));
    }
  }

  void _attachController(PluvioraController? value) {
    _ownsController = value == null;
    _controller = value ?? PluvioraController();
    _controller.attachDelegate(this);
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
        paused: !widget.autoplay,
        volume: _controller.musicVolume,
      );
      audio.setRelativePlaySpeed(_musicHandle!, _controller.playbackRate);
      _engine
        ..setNoteScale(_controller.noteScale)
        ..setFlowSpeed(1.66);

      final finalResult = PluvioraLoadResult(
        metadata: loadResult.metadata,
        warnings: List.unmodifiable([
          ...loadResult.warnings,
          ...resourceLoad.warnings,
        ]),
      );
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
    final audio = SoLoud.instance;
    final state = _controller.playbackState;
    if (state != PluvioraPlaybackState.playing && !_needsRender) return;
    try {
      final position = audio.getPosition(_musicHandle!);
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
    if (_controller.playbackState == PluvioraPlaybackState.completed) {
      await seek(Duration.zero);
    }
    SoLoud.instance.setPause(_musicHandle!, false);
    _controller.setPlaybackState(PluvioraPlaybackState.playing);
  }

  @override
  Future<void> pause() async {
    _ensureReady();
    SoLoud.instance.setPause(_musicHandle!, true);
    _controller.setPlaybackState(PluvioraPlaybackState.paused);
    _needsRender = true;
  }

  @override
  Future<void> seek(Duration position) async {
    _ensureReady();
    final clamped = Duration(
      microseconds: position.inMicroseconds.clamp(0, _duration.inMicroseconds),
    );
    SoLoud.instance.seek(_musicHandle!, clamped);
    _controller.setPosition(clamped);
    _needsRender = true;
  }

  @override
  Future<void> setPlaybackRate(double rate) async {
    _ensureReady();
    SoLoud.instance.setRelativePlaySpeed(_musicHandle!, rate);
  }

  @override
  Future<void> setMusicVolume(double volume) async {
    _ensureReady();
    SoLoud.instance.setVolume(_musicHandle!, volume);
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
    await _load(source ?? _source ?? widget.source);
  }

  @override
  Future<void> release() async {
    if (_released) return;
    _released = true;
    _loadToken++;
    _ticker.stop();
    await _disposeLoadedState();
    _engine.dispose();
    _controller.detachDelegate(this);
  }

  Future<void> _disposeLoadedState() async {
    final audio = SoLoud.instance;
    final handle = _musicHandle;
    final source = _musicSource;
    _musicHandle = null;
    _musicSource = null;
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
    _resources?.dispose();
    _resources = null;
    _ready = false;
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
