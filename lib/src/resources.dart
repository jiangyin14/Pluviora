import 'dart:ui' as ui;

import 'package:flutter/services.dart';
import 'package:flutter_avif/flutter_avif.dart';

import 'models.dart';
import 'source.dart';

final class PluvioraResourceLoad {
  const PluvioraResourceLoad(this.resources, this.warnings);

  final PluvioraPainterResources resources;
  final List<PluvioraWarning> warnings;
}

final class PluvioraPainterResources {
  PluvioraPainterResources({
    required this.trackAnchor,
    required this.pause,
    required this.notes,
    required this.hitRingShader,
    required this.illustration,
  });

  final ui.Image trackAnchor;
  final ui.Image pause;
  final Map<String, ui.Image> notes;
  final ui.FragmentShader? hitRingShader;
  final ui.Image? illustration;

  static Future<_BundledImages>? _bundledFuture;
  static Future<ui.FragmentProgram?>? _shaderFuture;

  static Future<PluvioraResourceLoad> load(PluvioraSource source) async {
    final warnings = <PluvioraWarning>[];
    final bundled = await (_bundledFuture ??= _loadBundledImages());
    final shader = await (_shaderFuture ??= _loadShader());

    ui.Image? illustration;
    if (source.background != null) {
      try {
        illustration = await decode(
          await source.background!.read(),
          name: source.background!.name,
        );
      } catch (error) {
        warnings.add(
          PluvioraWarning(
            code: 'illustration_decode_failed',
            message:
                'The background image could not be decoded; the fallback '
                'background is being used: $error',
            asset: source.background!.name,
          ),
        );
      }
    }

    return PluvioraResourceLoad(
      PluvioraPainterResources(
        trackAnchor: bundled.trackAnchor,
        pause: bundled.pause,
        notes: bundled.notes,
        hitRingShader: shader?.fragmentShader(),
        illustration: illustration,
      ),
      warnings,
    );
  }

  static Future<ui.Image> decode(
    Uint8List bytes, {
    required String name,
    int? maxWidth,
  }) async {
    if (name.toLowerCase().endsWith('.avif')) {
      final frames = await decodeAvif(bytes);
      if (frames.isEmpty) {
        throw StateError('The image contains no frames.');
      }
      for (final frame in frames.skip(1)) {
        frame.image.dispose();
      }
      return frames.first.image;
    }
    final codec = await ui.instantiateImageCodec(
      bytes,
      targetWidth: maxWidth,
      allowUpscaling: false,
    );
    try {
      return (await codec.getNextFrame()).image;
    } finally {
      codec.dispose();
    }
  }

  void dispose() {
    hitRingShader?.dispose();
    illustration?.dispose();
  }

  static Future<_BundledImages> _loadBundledImages() async {
    Future<ui.Image> load(String path) async {
      final data = await rootBundle.load('packages/pluviora/$path');
      return decode(data.buffer.asUint8List(), name: path);
    }

    final images = await Future.wait([
      load('files/resources/runtime/track_anchor.png'),
      load('files/resources/runtime/pause.png'),
      load('files/resources/runtime/notes/primary.png'),
      load('files/resources/runtime/notes/primary_double.png'),
      load('files/resources/runtime/notes/secondary.png'),
      load('files/resources/runtime/notes/sustained.png'),
      load('files/resources/runtime/notes/sustained_double.png'),
      load('files/resources/runtime/notes/accent.png'),
      load('files/resources/runtime/notes/accent_double.png'),
      load('files/resources/runtime/notes/accent_sustained.png'),
      load('files/resources/runtime/notes/accent_sustained_double.png'),
    ]);
    return _BundledImages(
      trackAnchor: images[0],
      pause: images[1],
      notes: Map.unmodifiable({
        'tap': images[2],
        'tap_double': images[3],
        'drag': images[4],
        'hold': images[5],
        'hold_double': images[6],
        'extap': images[7],
        'extap_double': images[8],
        'exhold': images[9],
        'exhold_double': images[10],
      }),
    );
  }

  static Future<ui.FragmentProgram?> _loadShader() async {
    try {
      return await ui.FragmentProgram.fromAsset(
        'packages/pluviora/shaders/hit_ring.frag',
      );
    } catch (_) {
      return null;
    }
  }
}

final class _BundledImages {
  const _BundledImages({
    required this.trackAnchor,
    required this.pause,
    required this.notes,
  });

  final ui.Image trackAnchor;
  final ui.Image pause;
  final Map<String, ui.Image> notes;
}
