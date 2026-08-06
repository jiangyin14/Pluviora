import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import '../pluviora_bindings_generated.dart' as native;
import 'models.dart';
import 'source.dart';

/// Low-level, independently owned Pluviora native engine.
final class PluvioraEngine {
  PluvioraEngine() : _handle = native.pluviora_create() {
    if (native.pluviora_abi_version() != native.PLUVIORA_ABI_VERSION) {
      throw const PluvioraException('原生 ABI 版本不匹配。');
    }
    if (_handle == nullptr) {
      throw const PluvioraException('无法创建原生 Pluviora 实例。');
    }
    _finalizer.attach(this, _handle, detach: this);
  }

  static final Finalizer<native.PluvioraHandle> _finalizer =
      Finalizer<native.PluvioraHandle>(native.pluviora_destroy);

  final native.PluvioraHandle _handle;
  bool _disposed = false;
  int _generation = 0;

  bool get isDisposed => _disposed;

  Future<PluvioraLoadResult> load(PluvioraSource source) async {
    _ensureAlive();
    final results = await Future.wait<Uint8List>([
      source.document.read(),
      if (source.orderingScript != null) source.orderingScript!.read(),
    ]);
    _ensureAlive();
    return loadBytes(
      results.first,
      orderingScript: source.orderingScript == null ? null : results[1],
    );
  }

  PluvioraLoadResult loadBytes(
    Uint8List documentJson, {
    Uint8List? orderingScript,
  }) {
    _ensureAlive();
    if (documentJson.isEmpty) {
      throw const PluvioraException('预览 JSON 为空。');
    }
    final jsonPointer = calloc<Uint8>(documentJson.length);
    final jsPointer = orderingScript == null || orderingScript.isEmpty
        ? nullptr.cast<Uint8>()
        : calloc<Uint8>(orderingScript.length);
    try {
      jsonPointer.asTypedList(documentJson.length).setAll(0, documentJson);
      if (orderingScript != null && orderingScript.isNotEmpty) {
        jsPointer.asTypedList(orderingScript.length).setAll(0, orderingScript);
      }
      _check(
        native.pluviora_load(
          _handle,
          jsonPointer,
          documentJson.length,
          jsPointer,
          orderingScript?.length ?? 0,
        ),
      );
    } finally {
      calloc.free(jsonPointer);
      if (jsPointer != nullptr) calloc.free(jsPointer);
    }
    _generation++;
    return PluvioraLoadResult(metadata: _metadata(), warnings: _warnings());
  }

  PluvioraFrameView render({
    required Duration position,
    required double width,
    required double height,
    Duration? songLength,
  }) {
    _ensureAlive();
    final output = calloc<native.PluvioraFrameView>();
    try {
      _check(
        native.pluviora_render(
          _handle,
          position.inMicroseconds / Duration.microsecondsPerSecond,
          width,
          height,
          (songLength ?? Duration.zero).inMicroseconds /
              Duration.microsecondsPerSecond,
          output,
        ),
      );
      final frame = output.ref;
      return PluvioraFrameView(
        bytes: frame.data.asTypedList(frame.length),
        commandCount: frame.command_count,
        hitCount: frame.hit_count,
        dragCount: frame.drag_count,
        fractureCount: frame.fracture_count,
        generation: _generation,
      );
    } finally {
      calloc.free(output);
    }
  }

  void setNoteScale(double scale) {
    _ensureAlive();
    _check(native.pluviora_set_note_scale(_handle, scale));
  }

  void setFlowSpeed(double speed) {
    _ensureAlive();
    _check(native.pluviora_set_flow_speed(_handle, speed));
  }

  void setStoryboardAssetSize(
    String name, {
    required int width,
    required int height,
  }) {
    _ensureAlive();
    final pointer = name.toNativeUtf8();
    try {
      _check(
        native.pluviora_set_storyboard_asset_size(
          _handle,
          pointer.cast<Char>(),
          width.toDouble(),
          height.toDouble(),
        ),
      );
    } finally {
      malloc.free(pointer);
    }
  }

  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _finalizer.detach(this);
    native.pluviora_destroy(_handle);
  }

  PluvioraMetadata _metadata() {
    final output = calloc<native.PluvioraMetadataView>();
    try {
      _check(native.pluviora_get_metadata(_handle, output));
      final metadata = output.ref;
      String text(Pointer<Char> pointer) =>
          pointer == nullptr ? '' : pointer.cast<Utf8>().toDartString();
      return PluvioraMetadata(
        title: text(metadata.title),
        composer: text(metadata.composer),
        illustrator: text(metadata.illustrator),
        beatmapper: text(metadata.beatmapper),
        difficulty: text(metadata.difficulty),
        audioFile: text(metadata.audio_file),
        illustrationFile: text(metadata.illustration_file),
        difficultyValue: metadata.difficulty_value,
        chartDuration: Duration(
          microseconds:
              (metadata.chart_duration * Duration.microsecondsPerSecond)
                  .round(),
        ),
        lineCount: metadata.line_count,
        noteCount: metadata.note_count,
        animationCount: metadata.animation_count,
        storyboardCount: metadata.storyboard_count,
        contentHash: metadata.content_hash,
      );
    } finally {
      calloc.free(output);
    }
  }

  List<PluvioraWarning> _warnings() {
    final count = native.pluviora_warning_count(_handle);
    return List<PluvioraWarning>.generate(count, (index) {
      final pointer = native.pluviora_warning_at(_handle, index);
      return PluvioraWarning(
        code: 'native_warning',
        message: pointer == nullptr ? '' : pointer.cast<Utf8>().toDartString(),
      );
    }, growable: false);
  }

  void _check(int status) {
    if (status == native.PLUVIORA_OK) return;
    final pointer = native.pluviora_last_error(_handle);
    final message = pointer == nullptr
        ? '原生核心返回错误。'
        : pointer.cast<Utf8>().toDartString();
    throw PluvioraException(message, status: status);
  }

  void _ensureAlive() {
    if (_disposed) throw const PluvioraException('PluvioraEngine 已释放。');
  }
}
