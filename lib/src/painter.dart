import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:flutter/rendering.dart';

import 'models.dart';
import 'resources.dart';

final class PluvioraFrameNotifier extends ChangeNotifier {
  PluvioraFrameView? frame;
  PluvioraPainterResources? resources;

  void setResources(PluvioraPainterResources value) {
    resources = value;
    notifyListeners();
  }

  void update(PluvioraFrameView value) {
    frame = value;
    notifyListeners();
  }
}

final class _TextBitmapMesh {
  _TextBitmapMesh(this.positions, this.coverages)
    : colors = Int32List(coverages.length * 6);

  final Float32List positions;
  final Uint8List coverages;
  final Int32List colors;
  int? _rgba;

  void applyColor(int rgba) {
    if (_rgba == rgba) return;
    _rgba = rgba;
    final red = (rgba >> 24) & 0xff;
    final green = (rgba >> 16) & 0xff;
    final blue = (rgba >> 8) & 0xff;
    final baseAlpha = rgba & 0xff;
    for (var run = 0; run < coverages.length; run++) {
      final alpha = (baseAlpha * coverages[run] / 255).round();
      final color = ((alpha << 24) | (red << 16) | (green << 8) | blue)
          .toSigned(32);
      colors.fillRange(run * 6, run * 6 + 6, color);
    }
  }
}

final class PluvioraPainter extends CustomPainter {
  PluvioraPainter(this.frames) : super(repaint: frames);

  final PluvioraFrameNotifier frames;
  final Float32List _quadPositions = Float32List(8);
  final Int32List _quadColors = Int32List(4);
  final Map<String, _TextBitmapMesh> _textBitmapCache = {};
  static final List<double> _backgroundMaskStops = List.generate(
    128,
    (index) => index / 127,
    growable: false,
  );
  static final List<ui.Color> _backgroundMaskColors = List.generate(128, (
    index,
  ) {
    final normalized = index / 127 * 1.2 - 0.2;
    final alpha = normalized <= 0
        ? 0
        : (math.pow(normalized, 0.8) * 270).toInt().clamp(0, 255);
    return ui.Color.fromARGB(alpha, 0, 0, 0);
  }, growable: false);
  static final List<double> _progressStops = List.generate(
    128,
    (index) => index / 127,
    growable: false,
  );
  static final List<ui.Color> _progressColors = List.generate(128, (index) {
    final progress = index / 127;
    final alpha = ((1 - math.pow(1 - progress, 2.2)) * 255).toInt().clamp(
      0,
      255,
    );
    return ui.Color.fromARGB(alpha, 255, 255, 255);
  }, growable: false);

  @override
  void paint(ui.Canvas canvas, ui.Size size) {
    final resources = frames.resources;
    if (resources == null) {
      _drawDefaultBackground(canvas, size);
      return;
    }
    _drawBackground(canvas, size, resources.illustration);
    final frame = frames.frame;
    if (frame == null || frame.bytes.isEmpty) return;
    final data = ByteData.sublistView(frame.bytes);
    var offset = 0;
    while (offset + 8 <= data.lengthInBytes) {
      final type = data.getUint16(offset, Endian.little);
      final length = data.getUint32(offset + 4, Endian.little);
      if (length < 8 || offset + length > data.lengthInBytes) break;
      if (type == 4) {
        offset = _drawNoteBatch(canvas, data, offset, resources);
        continue;
      }
      _drawCommand(canvas, data, offset, type, resources);
      offset = _aligned(offset + length);
    }
  }

  int _drawNoteBatch(
    ui.Canvas canvas,
    ByteData data,
    int start,
    PluvioraPainterResources resources,
  ) {
    var offset = start;
    while (offset + 8 <= data.lengthInBytes &&
        data.getUint16(offset, Endian.little) == 4) {
      final length = data.getUint32(offset + 4, Endian.little);
      if (length < 48 || offset + length > data.lengthInBytes) break;
      final payload = offset + 8;
      final x = _f(data, payload);
      final y = _f(data, payload + 4);
      final rotation = _f(data, payload + 8) * math.pi / 180;
      final width = _f(data, payload + 12);
      final head = _f(data, payload + 16);
      final body = _f(data, payload + 20);
      final tail = _f(data, payload + 24);
      final rgba = data.getUint32(payload + 28, Endian.little);
      final kind = data.getUint32(payload + 32, Endian.little);
      final flags = data.getUint32(payload + 36, Endian.little);
      final name = _noteSprite(kind, flags);
      final image = resources.notes[name]!;
      final cutPadding = kind == 1 ? 668.0 : image.width / 2.0;
      final imageWidth = image.width.toDouble();
      final imageHeight = image.height.toDouble();
      final paint = ui.Paint()
        ..filterQuality = ui.FilterQuality.medium
        ..colorFilter = ui.ColorFilter.mode(
          _color(rgba),
          ui.BlendMode.modulate,
        );
      canvas.save();
      canvas.translate(x, y);
      canvas.rotate(rotation);
      _drawImageSlice(
        canvas,
        image,
        ui.Rect.fromLTWH(0, 0, cutPadding, imageHeight),
        ui.Rect.fromLTWH(-head, -width / 2, head, width),
        paint,
      );
      _drawImageSlice(
        canvas,
        image,
        ui.Rect.fromLTWH(
          cutPadding,
          0,
          imageWidth - cutPadding * 2,
          imageHeight,
        ),
        ui.Rect.fromLTWH(0, -width / 2, body, width),
        paint,
      );
      _drawImageSlice(
        canvas,
        image,
        ui.Rect.fromLTWH(imageWidth - cutPadding, 0, cutPadding, imageHeight),
        ui.Rect.fromLTWH(body, -width / 2, tail, width),
        paint,
      );
      canvas.restore();
      offset = _aligned(offset + length);
    }
    return offset;
  }

  static void _drawImageSlice(
    ui.Canvas canvas,
    ui.Image image,
    ui.Rect source,
    ui.Rect destination,
    ui.Paint paint,
  ) {
    if (source.width <= 0 ||
        source.height <= 0 ||
        destination.width == 0 ||
        destination.height == 0) {
      return;
    }
    canvas.drawImageRect(image, source, destination, paint);
  }

  void _drawCommand(
    ui.Canvas canvas,
    ByteData data,
    int offset,
    int type,
    PluvioraPainterResources resources,
  ) {
    final payload = offset + 8;
    final flags = data.getUint16(offset + 2, Endian.little);
    switch (type) {
      case 1:
        final x = _f(data, payload);
        final y = _f(data, payload + 4);
        final width = _f(data, payload + 8);
        final height = _f(data, payload + 12);
        final rotation = _f(data, payload + 16) * math.pi / 180;
        canvas.save();
        canvas.translate(x, y);
        canvas.rotate(rotation);
        final rect = ui.Rect.fromCenter(
          center: ui.Offset.zero,
          width: width,
          height: height,
        );
        canvas.drawRect(
          rect,
          flags & 1 == 0
              ? (ui.Paint()
                  ..color = _color(data.getUint32(payload + 20, Endian.little)))
              : (ui.Paint()
                  ..shader = ui.Gradient.linear(
                    rect.centerLeft,
                    rect.centerRight,
                    _progressColors,
                    _progressStops,
                  )),
        );
        canvas.restore();
      case 2:
        _drawQuad(canvas, data, payload);
      case 3:
        _drawSprite(canvas, data, payload, resources);
      case 7:
        _drawHitRing(canvas, data, payload, resources);
      case 8:
        _drawParticle(canvas, data, payload);
      case 9:
        _drawTextBitmap(canvas, data, payload);
    }
  }

  void _drawQuad(ui.Canvas canvas, ByteData data, int payload) {
    for (var index = 0; index < 8; index++) {
      _quadPositions[index] = _f(data, payload + index * 4);
    }
    final color = _argb(
      data.getUint32(payload + 32, Endian.little),
    ).toSigned(32);
    _quadColors.fillRange(0, 4, color);
    canvas.drawVertices(
      ui.Vertices.raw(
        ui.VertexMode.triangleFan,
        _quadPositions,
        colors: _quadColors,
      ),
      ui.BlendMode.srcOver,
      ui.Paint(),
    );
  }

  void _drawSprite(
    ui.Canvas canvas,
    ByteData data,
    int payload,
    PluvioraPainterResources resources,
  ) {
    final x = _f(data, payload);
    final y = _f(data, payload + 4);
    final width = _f(data, payload + 8);
    final height = _f(data, payload + 12);
    final rotation = _f(data, payload + 16) * math.pi / 180;
    final color = _color(data.getUint32(payload + 20, Endian.little));
    final kind = data.getUint32(payload + 24, Endian.little);
    final image = kind == 0 ? resources.trackAnchor : resources.pause;
    canvas.save();
    canvas.translate(x, y);
    canvas.rotate(rotation);
    canvas.drawImageRect(
      image,
      ui.Rect.fromLTWH(0, 0, image.width.toDouble(), image.height.toDouble()),
      ui.Rect.fromCenter(center: ui.Offset.zero, width: width, height: height),
      ui.Paint()
        ..filterQuality = ui.FilterQuality.medium
        ..colorFilter = ui.ColorFilter.mode(color, ui.BlendMode.modulate),
    );
    canvas.restore();
  }

  void _drawTextBitmap(ui.Canvas canvas, ByteData data, int payload) {
    final x = _f(data, payload);
    final y = _f(data, payload + 4);
    final rotation = _f(data, payload + 8) * math.pi / 180;
    final pixelScaleX = _f(data, payload + 12);
    final pixelScaleY = _f(data, payload + 16);
    final anchorX = _f(data, payload + 20);
    final anchorY = _f(data, payload + 24);
    final rgba = data.getUint32(payload + 28, Endian.little);
    final bitmapWidth = data.getUint32(payload + 32, Endian.little);
    final bitmapHeight = data.getUint32(payload + 36, Endian.little);
    final runCount = data.getUint32(payload + 40, Endian.little);
    final keyLow = data.getUint32(payload + 44, Endian.little);
    final keyHigh = data.getUint32(payload + 48, Endian.little);
    final cacheKey = '$keyHigh:$keyLow:$bitmapWidth:$bitmapHeight:$runCount';
    var mesh = _textBitmapCache[cacheKey];
    if (mesh == null) {
      if (_textBitmapCache.length >= 128) {
        _textBitmapCache.remove(_textBitmapCache.keys.first);
      }
      final positions = Float32List(runCount * 12);
      final coverages = Uint8List(runCount);
      var runOffset = payload + 52;
      for (var run = 0; run < runCount; run++) {
        final left = data.getUint32(runOffset, Endian.little).toDouble();
        final top = data.getUint32(runOffset + 4, Endian.little).toDouble();
        final right =
            left + data.getUint32(runOffset + 8, Endian.little).toDouble();
        final bottom = top + 1;
        coverages[run] = data.getUint32(runOffset + 12, Endian.little);
        final vertex = run * 12;
        positions
          ..[vertex] = left
          ..[vertex + 1] = top
          ..[vertex + 2] = right
          ..[vertex + 3] = top
          ..[vertex + 4] = right
          ..[vertex + 5] = bottom
          ..[vertex + 6] = left
          ..[vertex + 7] = top
          ..[vertex + 8] = right
          ..[vertex + 9] = bottom
          ..[vertex + 10] = left
          ..[vertex + 11] = bottom;
        runOffset += 16;
      }
      mesh = _TextBitmapMesh(positions, coverages);
      _textBitmapCache[cacheKey] = mesh;
    }
    mesh.applyColor(rgba);
    canvas.save();
    canvas.translate(x, y);
    canvas.rotate(rotation);
    canvas.scale(pixelScaleX, pixelScaleY);
    canvas.translate(-bitmapWidth * anchorX, -bitmapHeight * anchorY);
    canvas.drawVertices(
      ui.Vertices.raw(
        ui.VertexMode.triangles,
        mesh.positions,
        colors: mesh.colors,
      ),
      ui.BlendMode.srcOver,
      ui.Paint()..isAntiAlias = false,
    );
    canvas.restore();
  }

  void _drawHitRing(
    ui.Canvas canvas,
    ByteData data,
    int payload,
    PluvioraPainterResources resources,
  ) {
    final x = _f(data, payload);
    final y = _f(data, payload + 4);
    final size = _f(data, payload + 8);
    final progress = _f(data, payload + 12);
    final rotation = _f(data, payload + 16) * math.pi / 180;
    final seed = _f(data, payload + 20);
    final color = _color(data.getUint32(payload + 24, Endian.little));
    final rect = ui.Rect.fromLTWH(0, 0, size, size);
    canvas.save();
    canvas.translate(x, y);
    canvas.rotate(rotation);
    canvas.translate(-size / 2, -size / 2);
    final shader = resources.hitRingShader;
    if (shader == null) {
      canvas.drawCircle(
        ui.Offset(size / 2, size / 2),
        size * (0.18 + progress * 0.29),
        ui.Paint()
          ..style = ui.PaintingStyle.stroke
          ..strokeWidth = size * (0.075 - progress * 0.06)
          ..color = color.withValues(alpha: color.a * (1 - progress)),
      );
    } else {
      final textureIndex = (progress * 60).floor().clamp(0, 59);
      shader
        ..setFloat(0, size)
        ..setFloat(1, size)
        ..setFloat(2, textureIndex / 59)
        ..setFloat(3, seed)
        ..setFloat(4, color.r)
        ..setFloat(5, color.g)
        ..setFloat(6, color.b)
        ..setFloat(7, color.a);
      canvas.drawRect(rect, ui.Paint()..shader = shader);
    }
    canvas.restore();
  }

  void _drawParticle(ui.Canvas canvas, ByteData data, int payload) {
    final x = _f(data, payload);
    final y = _f(data, payload + 4);
    final radiusX = _f(data, payload + 8);
    final radiusY = _f(data, payload + 12);
    final rotation = _f(data, payload + 16) * math.pi / 180;
    canvas.save();
    canvas.translate(x, y);
    canvas.rotate(rotation);
    canvas.drawOval(
      ui.Rect.fromCenter(
        center: ui.Offset.zero,
        width: radiusX * 2,
        height: radiusY * 2,
      ),
      ui.Paint()..color = _color(data.getUint32(payload + 20, Endian.little)),
    );
    canvas.restore();
  }

  void _drawBackground(ui.Canvas canvas, ui.Size size, ui.Image? image) {
    late final ui.Rect destination;
    if (image == null) {
      _drawDefaultBackground(canvas, size);
      destination = ui.Offset.zero & size;
    } else {
      final sourceSize = ui.Size(
        image.width.toDouble(),
        image.height.toDouble(),
      );
      final fitted = applyBoxFit(BoxFit.cover, sourceSize, size);
      final source = Alignment.center.inscribe(
        fitted.source,
        ui.Offset.zero & sourceSize,
      );
      destination = Alignment.center.inscribe(
        fitted.destination,
        ui.Offset.zero & size,
      );
      canvas.drawImageRect(
        image,
        source,
        destination,
        ui.Paint()..filterQuality = ui.FilterQuality.medium,
      );
    }
    canvas.drawRect(
      destination,
      ui.Paint()
        ..shader = ui.Gradient.linear(
          destination.topCenter,
          destination.bottomCenter,
          _backgroundMaskColors,
          _backgroundMaskStops,
        ),
    );
  }

  void _drawDefaultBackground(ui.Canvas canvas, ui.Size size) {
    canvas.drawRect(
      ui.Offset.zero & size,
      ui.Paint()
        ..shader = ui.Gradient.linear(
          ui.Offset.zero,
          ui.Offset(size.width, size.height),
          const [ui.Color(0xff091124), ui.Color(0xff1e3151)],
        ),
    );
  }

  static String _noteSprite(int kind, int flags) {
    final simultaneous = flags & 1 != 0;
    final alwaysPerfect = flags & 2 != 0;
    if (kind == 1) {
      return '${alwaysPerfect ? 'ex' : ''}hold${simultaneous ? '_double' : ''}';
    }
    if (kind == 2) return 'drag';
    if (kind == 3 || alwaysPerfect) {
      return 'extap${simultaneous ? '_double' : ''}';
    }
    return 'tap${simultaneous ? '_double' : ''}';
  }

  static int _aligned(int value) => (value + 3) & ~3;
  static double _f(ByteData data, int offset) =>
      data.getFloat32(offset, Endian.little);

  static int _argb(int rgba) =>
      ((rgba & 0xff) << 24) | ((rgba >> 8) & 0x00ffffff);
  static ui.Color _color(int rgba) => ui.Color(_argb(rgba));

  @override
  bool shouldRepaint(covariant PluvioraPainter oldDelegate) => false;
}
