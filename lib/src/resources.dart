import 'dart:convert';
import 'dart:ui' as ui;

import 'package:flutter/services.dart';

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
}
