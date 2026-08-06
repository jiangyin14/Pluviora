import 'dart:io';
import 'dart:typed_data';

/// A path-backed or memory-backed input used by [PluvioraSource].
final class PluvioraAsset {
  const PluvioraAsset._({required this.name, this.path, this.bytes});

  factory PluvioraAsset.file(String path, {String? name}) =>
      PluvioraAsset._(name: name ?? _basename(path), path: path);

  factory PluvioraAsset.memory(Uint8List bytes, {required String name}) =>
      PluvioraAsset._(name: name, bytes: bytes);

  final String name;
  final String? path;
  final Uint8List? bytes;

  Future<Uint8List> read() async => bytes ?? File(path!).readAsBytes();

  static String _basename(String path) =>
      path.replaceAll('\\', '/').split('/').last;
}

/// Complete input for a supported special-file preview.
///
/// [document] contains the preview data. The optional [orderingScript] companion
/// is scanned only for supported static ordering hints and is never executed.
final class PluvioraSource {
  const PluvioraSource({
    required this.document,
    required this.audio,
    this.orderingScript,
    this.background,
    this.overlayAssets = const {},
  });

  factory PluvioraSource.files({
    required String document,
    required String audio,
    String? orderingScript,
    String? background,
    Map<String, String> overlayAssets = const {},
  }) => PluvioraSource(
    document: PluvioraAsset.file(document),
    audio: PluvioraAsset.file(audio),
    orderingScript: orderingScript == null
        ? null
        : PluvioraAsset.file(orderingScript),
    background: background == null ? null : PluvioraAsset.file(background),
    overlayAssets: overlayAssets.map(
      (name, path) => MapEntry(name, PluvioraAsset.file(path, name: name)),
    ),
  );

  final PluvioraAsset document;
  final PluvioraAsset audio;
  final PluvioraAsset? orderingScript;
  final PluvioraAsset? background;
  final Map<String, PluvioraAsset> overlayAssets;
}
