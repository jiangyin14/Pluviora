import 'dart:typed_data';

/// Current lifecycle state of a chart load operation.
enum PluvioraLoadState { idle, loading, ready, failed, disposed }

/// Current playback state reported by the player controller.
enum PluvioraPlaybackState { stopped, playing, paused, completed }

/// A non-fatal issue encountered while loading chart resources.
final class PluvioraWarning {
  const PluvioraWarning({
    required this.code,
    required this.message,
    this.asset,
  });

  final String code;
  final String message;
  final String? asset;

  @override
  String toString() => message;
}

/// Immutable metadata parsed from a supported preview document.
final class PluvioraMetadata {
  const PluvioraMetadata({
    required this.title,
    required this.composer,
    required this.illustrator,
    required this.beatmapper,
    required this.difficulty,
    required this.audioFile,
    required this.illustrationFile,
    required this.difficultyValue,
    required this.chartDuration,
    required this.lineCount,
    required this.noteCount,
    required this.animationCount,
    required this.storyboardCount,
    required this.contentHash,
  });

  final String title;
  final String composer;
  final String illustrator;
  final String beatmapper;
  final String difficulty;
  final String audioFile;
  final String illustrationFile;
  final double difficultyValue;
  final Duration chartDuration;
  final int lineCount;
  final int noteCount;
  final int animationCount;
  final int storyboardCount;
  final int contentHash;
}

/// Metadata and non-fatal warnings returned by a successful load.
final class PluvioraLoadResult {
  const PluvioraLoadResult({required this.metadata, this.warnings = const []});

  final PluvioraMetadata metadata;
  final List<PluvioraWarning> warnings;

  bool get hasWarnings => warnings.isNotEmpty;
}

/// A zero-copy view of one native frame.
///
/// The buffer remains valid until the same engine renders again or is
/// disposed. Consumers that need a longer lifetime must copy [bytes].
final class PluvioraFrameView {
  const PluvioraFrameView({
    required this.bytes,
    required this.commandCount,
    required this.hitCount,
    required this.dragCount,
    required this.fractureCount,
    required this.generation,
  });

  final Uint8List bytes;
  final int commandCount;
  final int hitCount;
  final int dragCount;
  final int fractureCount;
  final int generation;
}

/// An error raised by the Dart wrapper or native Pluviora core.
final class PluvioraException implements Exception {
  const PluvioraException(this.message, {this.status});

  final String message;
  final int? status;

  @override
  String toString() => status == null
      ? 'PluvioraException: $message'
      : 'PluvioraException($status): $message';
}
