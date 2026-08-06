import 'dart:convert';
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
    required this.atlas,
    required this.hitRingShader,
    required this.illustration,
    required this.storyboards,
  });

  final ui.Image atlas;
  final ui.FragmentShader? hitRingShader;
  final ui.Image? illustration;
  final Map<String, ui.Image> storyboards;

  static Future<ui.Image>? _atlasFuture;
  static Future<ui.FragmentProgram?>? _shaderFuture;

  static Future<PluvioraResourceLoad> load(
    PluvioraSource source,
    Uint8List documentJson,
  ) async {
    final warnings = <PluvioraWarning>[];
    final atlas = await (_atlasFuture ??= _loadAtlas());
    final shader = await (_shaderFuture ??= _loadShader());

    ui.Image? illustration;
    if (source.background != null) {
      try {
        illustration = await decode(
          await source.background!.read(),
          name: source.background!.name,
          maxWidth: 2048,
        );
      } catch (error) {
        warnings.add(
          PluvioraWarning(
            code: 'illustration_decode_failed',
            message: '背景图片解码失败，已使用默认背景：$error',
            asset: source.background!.name,
          ),
        );
      }
    }

    final storyboards = <String, ui.Image>{};
    for (final entry in source.overlayAssets.entries) {
      try {
        storyboards[entry.key] = await decode(
          await entry.value.read(),
          name: entry.value.name,
          maxWidth: 1024,
        );
      } catch (error) {
        warnings.add(
          PluvioraWarning(
            code: 'storyboard_decode_failed',
            message: '覆盖素材解码失败，已跳过 ${entry.key}：$error',
            asset: entry.key,
          ),
        );
      }
    }

    for (final name in _requiredStoryboardAssets(documentJson)) {
      if (!storyboards.containsKey(name)) {
        warnings.add(
          PluvioraWarning(
            code: 'storyboard_asset_missing',
            message: '缺少覆盖素材，播放时将跳过：$name',
            asset: name,
          ),
        );
      }
    }

    return PluvioraResourceLoad(
      PluvioraPainterResources(
        atlas: atlas,
        hitRingShader: shader?.fragmentShader(),
        illustration: illustration,
        storyboards: storyboards,
      ),
      warnings,
    );
  }

  static Future<ui.Image> decode(
    Uint8List bytes, {
    required String name,
    int? maxWidth,
  }) async {
    if (_isAvif(bytes, name)) {
      final frames = await decodeAvif(bytes);
      if (frames.isEmpty) throw StateError('AVIF 不包含可用帧');
      final image = frames.first.image;
      for (final frame in frames.skip(1)) {
        frame.image.dispose();
      }
      if (maxWidth != null && image.width > maxWidth) {
        final height = (image.height * maxWidth / image.width).round();
        final recorder = ui.PictureRecorder();
        ui.Canvas(recorder).drawImageRect(
          image,
          ui.Rect.fromLTWH(
            0,
            0,
            image.width.toDouble(),
            image.height.toDouble(),
          ),
          ui.Rect.fromLTWH(0, 0, maxWidth.toDouble(), height.toDouble()),
          ui.Paint()..filterQuality = ui.FilterQuality.high,
        );
        final picture = recorder.endRecording();
        final resized = await picture.toImage(maxWidth, height);
        picture.dispose();
        image.dispose();
        return resized;
      }
      return image;
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
    for (final image in storyboards.values) {
      image.dispose();
    }
  }

  static Future<ui.Image> _loadAtlas() async {
    final data = await rootBundle.load(
      'packages/pluviora/files/resources/default/preview_atlas.png',
    );
    return decode(data.buffer.asUint8List(), name: 'preview_atlas.png');
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

  static Set<String> _requiredStoryboardAssets(Uint8List chartJson) {
    try {
      final root = jsonDecode(utf8.decode(chartJson)) as Map<String, dynamic>;
      final objects = root['storyboardObjects'] as List<dynamic>? ?? const [];
      return objects
          .whereType<Map<String, dynamic>>()
          .where((object) => object['type'] == 0)
          .map((object) => object['data'])
          .whereType<String>()
          .where((name) => name.isNotEmpty)
          .where((name) => !name.startsWith('builtin.'))
          .toSet();
    } catch (_) {
      return const <String>{};
    }
  }

  static bool _isAvif(Uint8List bytes, String name) {
    if (name.toLowerCase().endsWith('.avif')) return true;
    if (bytes.length < 12) return false;
    return ascii
            .decode(bytes.sublist(4, 12), allowInvalid: true)
            .contains('ftyp') &&
        ascii
            .decode(
              bytes.sublist(8, 16.clamp(0, bytes.length)),
              allowInvalid: true,
            )
            .contains(RegExp('avi[fs]'));
  }
}
