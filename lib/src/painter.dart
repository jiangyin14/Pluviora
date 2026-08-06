import 'dart:convert';
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

final class PluvioraPainter extends CustomPainter {
  PluvioraPainter(this.frames) : super(repaint: frames);

  final PluvioraFrameNotifier frames;
  Float32List _atlasTransforms = Float32List(256);
  Float32List _atlasRects = Float32List(256);
  Int32List _atlasColors = Int32List(64);
  final Float32List _quadPositions = Float32List(8);
  final Float32List _quadTextures = Float32List(8);
  final Int32List _quadColors = Int32List(4);
  final Float64List _identity = Float64List.fromList(const [
    1,
    0,
    0,
    0,
    0,
    1,
    0,
    0,
    0,
    0,
    1,
    0,
    0,
    0,
    0,
    1,
  ]);
  final Map<String, TextPainter> _textCache = {};

  static const Map<String, ui.Rect> _atlas = {
    'line_head': ui.Rect.fromLTWH(0, 0, 192, 192),
    'pause': ui.Rect.fromLTWH(192, 0, 128, 128),
    'drag': ui.Rect.fromLTWH(320, 0, 192, 192),
    'tap': ui.Rect.fromLTWH(512, 0, 192, 192),
    'tap_double': ui.Rect.fromLTWH(704, 0, 192, 192),
    'extap': ui.Rect.fromLTWH(896, 0, 192, 192),
    'extap_double': ui.Rect.fromLTWH(1088, 0, 192, 192),
    'hold': ui.Rect.fromLTWH(0, 256, 576, 192),
    'hold_double': ui.Rect.fromLTWH(576, 256, 576, 192),
    'exhold': ui.Rect.fromLTWH(1152, 256, 576, 192),
    'exhold_double': ui.Rect.fromLTWH(0, 448, 576, 192),
  };

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
    var atlasCount = 0;

    void flushAtlas() {
      if (atlasCount == 0) return;
      canvas.drawRawAtlas(
        resources.atlas,
        Float32List.sublistView(_atlasTransforms, 0, atlasCount * 4),
        Float32List.sublistView(_atlasRects, 0, atlasCount * 4),
        Int32List.sublistView(_atlasColors, 0, atlasCount),
        ui.BlendMode.modulate,
        null,
        ui.Paint()..filterQuality = ui.FilterQuality.medium,
      );
      atlasCount = 0;
    }

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
      final source = _atlas[name]!;
      if (kind == 1) {
        flushAtlas();
        canvas.save();
        canvas.translate(x, y);
        canvas.rotate(rotation);
        canvas.drawImageRect(
          resources.atlas,
          source,
          ui.Rect.fromLTWH(-head, -width / 2, head + body + tail, width),
          ui.Paint()
            ..filterQuality = ui.FilterQuality.medium
            ..colorFilter = ui.ColorFilter.mode(
              _color(rgba),
              ui.BlendMode.modulate,
            ),
        );
        canvas.restore();
      } else {
        _ensureAtlasCapacity(atlasCount + 1);
        final scale = width / source.width;
        final cosine = math.cos(rotation) * scale;
        final sine = math.sin(rotation) * scale;
        final transform = atlasCount * 4;
        _atlasTransforms[transform] = cosine;
        _atlasTransforms[transform + 1] = sine;
        _atlasTransforms[transform + 2] =
            x - cosine * source.center.dx + sine * source.center.dy;
        _atlasTransforms[transform + 3] =
            y - sine * source.center.dx - cosine * source.center.dy;
        _atlasRects[transform] = source.left;
        _atlasRects[transform + 1] = source.top;
        _atlasRects[transform + 2] = source.right;
        _atlasRects[transform + 3] = source.bottom;
        _atlasColors[atlasCount] = _argb(rgba).toSigned(32);
        atlasCount++;
      }
      offset = _aligned(offset + length);
    }
    flushAtlas();
    return offset;
  }

  void _drawCommand(
    ui.Canvas canvas,
    ByteData data,
    int offset,
    int type,
    PluvioraPainterResources resources,
  ) {
    final payload = offset + 8;
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
        canvas.drawRect(
          ui.Rect.fromCenter(
            center: ui.Offset.zero,
            width: width,
            height: height,
          ),
          ui.Paint()
            ..color = _color(data.getUint32(payload + 20, Endian.little)),
        );
        canvas.restore();
      case 2:
        _drawQuad(canvas, data, payload);
      case 3:
        _drawSprite(canvas, data, payload, resources);
      case 5:
        _drawText(canvas, data, offset, payload);
      case 6:
        _drawStoryboardImage(canvas, data, offset, payload, resources);
      case 7:
        _drawHitRing(canvas, data, payload, resources);
      case 8:
        _drawParticle(canvas, data, payload);
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
    final source = _atlas[kind == 0 ? 'line_head' : 'pause']!;
    canvas.save();
    canvas.translate(x, y);
    canvas.rotate(rotation);
    canvas.drawImageRect(
      resources.atlas,
      source,
      ui.Rect.fromCenter(center: ui.Offset.zero, width: width, height: height),
      ui.Paint()
        ..filterQuality = ui.FilterQuality.medium
        ..colorFilter = ui.ColorFilter.mode(color, ui.BlendMode.modulate),
    );
    canvas.restore();
  }

  void _drawText(ui.Canvas canvas, ByteData data, int command, int payload) {
    final x = _f(data, payload);
    final y = _f(data, payload + 4);
    final size = _f(data, payload + 8);
    final rotation = _f(data, payload + 12) * math.pi / 180;
    final scaleX = _f(data, payload + 16);
    final scaleY = _f(data, payload + 20);
    final anchorX = _f(data, payload + 24);
    final anchorY = _f(data, payload + 28);
    final rgba = data.getUint32(payload + 32, Endian.little);
    final textLength = data.getUint32(payload + 36, Endian.little);
    final textStart = payload + 40;
    final text = utf8.decode(
      data.buffer.asUint8List(data.offsetInBytes + textStart, textLength),
      allowMalformed: true,
    );
    final cacheKey = '$text\u0000$size\u0000$rgba';
    if (!_textCache.containsKey(cacheKey) && _textCache.length >= 256) {
      _textCache.clear();
    }
    final painter = _textCache.putIfAbsent(cacheKey, () {
      final value = TextPainter(
        text: TextSpan(
          text: text,
          style: TextStyle(
            color: _color(rgba),
            package: 'pluviora',
            fontSize: size,
          ),
        ),
        textDirection: TextDirection.ltr,
        maxLines: 1,
      )..layout();
      return value;
    });
    canvas.save();
    canvas.translate(x, y);
    canvas.rotate(rotation);
    canvas.scale(scaleX, scaleY);
    painter.paint(
      canvas,
      ui.Offset(-painter.width * anchorX, -painter.height * anchorY),
    );
    canvas.restore();
  }

  void _drawStoryboardImage(
    ui.Canvas canvas,
    ByteData data,
    int command,
    int payload,
    PluvioraPainterResources resources,
  ) {
    for (var index = 0; index < 8; index++) {
      _quadPositions[index] = _f(data, payload + index * 4);
    }
    final rgba = data.getUint32(payload + 32, Endian.little);
    final builtin = data.getUint32(payload + 36, Endian.little);
    final nameLength = data.getUint32(payload + 40, Endian.little);
    final nameStart = payload + 44;
    final name = utf8.decode(
      data.buffer.asUint8List(data.offsetInBytes + nameStart, nameLength),
      allowMalformed: true,
    );
    if (builtin == 1 || builtin == 2 || builtin == 3 || builtin == 5) {
      final color = _argb(rgba).toSigned(32);
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
      return;
    }
    ui.Image? image;
    ui.Rect? source;
    if (builtin == 4) {
      image = resources.atlas;
      source = _atlas['line_head'];
    } else if (builtin >= 16 && builtin <= 22) {
      image = resources.atlas;
      source =
          _atlas[switch (builtin) {
            16 => 'tap',
            17 => 'hold',
            18 => 'drag',
            19 => 'extap',
            20 => 'tap_double',
            21 => 'extap_double',
            _ => 'exhold',
          }];
    } else {
      image = resources.storyboards[name];
    }
    if (image == null) return;
    source ??= ui.Rect.fromLTWH(
      0,
      0,
      image.width.toDouble(),
      image.height.toDouble(),
    );
    _quadTextures
      ..[0] = source.left
      ..[1] = source.top
      ..[2] = source.right
      ..[3] = source.top
      ..[4] = source.right
      ..[5] = source.bottom
      ..[6] = source.left
      ..[7] = source.bottom;
    final color = _argb(rgba).toSigned(32);
    _quadColors.fillRange(0, 4, color);
    final vertices = ui.Vertices.raw(
      ui.VertexMode.triangleFan,
      _quadPositions,
      colors: _quadColors,
      textureCoordinates: _quadTextures,
    );
    canvas.drawVertices(
      vertices,
      ui.BlendMode.modulate,
      ui.Paint()
        ..shader = ui.ImageShader(
          image,
          ui.TileMode.clamp,
          ui.TileMode.clamp,
          _identity,
        ),
    );
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
    final color = _color(data.getUint32(payload + 20, Endian.little));
    final rect = ui.Rect.fromLTWH(0, 0, size, size);
    canvas.save();
    canvas.translate(x - size / 2, y - size / 2);
    canvas.rotate(rotation);
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
      shader
        ..setFloat(0, size)
        ..setFloat(1, size)
        ..setFloat(2, progress)
        ..setFloat(3, color.r)
        ..setFloat(4, color.g)
        ..setFloat(5, color.b)
        ..setFloat(6, color.a);
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
    if (image == null) {
      _drawDefaultBackground(canvas, size);
      return;
    }
    paintImage(
      canvas: canvas,
      rect: ui.Offset.zero & size,
      image: image,
      fit: BoxFit.cover,
      filterQuality: ui.FilterQuality.medium,
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

  void _ensureAtlasCapacity(int count) {
    if (_atlasColors.length >= count) return;
    var capacity = _atlasColors.length;
    while (capacity < count) {
      capacity *= 2;
    }
    _atlasTransforms = Float32List(capacity * 4);
    _atlasRects = Float32List(capacity * 4);
    _atlasColors = Int32List(capacity);
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
