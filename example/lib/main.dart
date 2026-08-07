import 'dart:async';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:pluviora/pluviora.dart';

void main() => runApp(const PluvioraExampleApp());

class PluvioraExampleApp extends StatelessWidget {
  const PluvioraExampleApp({super.key});

  @override
  Widget build(BuildContext context) => MaterialApp(
    title: 'Pluviora',
    debugShowCheckedModeBanner: false,
    theme: ThemeData(
      brightness: Brightness.dark,
      colorSchemeSeed: const Color(0xff83b8ff),
      scaffoldBackgroundColor: const Color(0xff091124),
    ),
    home: const PreviewHome(),
  );
}

class PreviewHome extends StatefulWidget {
  const PreviewHome({super.key});

  @override
  State<PreviewHome> createState() => _PreviewHomeState();
}

class _PreviewHomeState extends State<PreviewHome> {
  String? _json;
  String? _js;
  String? _music;
  String? _illustration;
  PluvioraSource? _source;
  PluvioraController _controller = PluvioraController();
  Timer? _refresh;

  static const _audioExtensions = ['ogg', 'opus', 'wav', 'mp3', 'flac', 'm4a'];
  static const _imageExtensions = ['avif', 'png', 'jpg', 'jpeg', 'webp'];

  @override
  void initState() {
    super.initState();
    _refresh = Timer.periodic(const Duration(milliseconds: 100), (_) {
      if (mounted && _source != null) setState(() {});
    });
  }

  Future<String?> _pickOne(List<String> extensions) async {
    final result = await FilePicker.pickFiles(
      type: FileType.custom,
      allowedExtensions: extensions,
    );
    return result?.files.single.path;
  }

  Future<void> _pickBundle() async {
    final result = await FilePicker.pickFiles(
      allowMultiple: true,
      type: FileType.any,
    );
    final paths = result?.paths.whereType<String>().toList() ?? const [];
    if (paths.isEmpty) return;
    final jsons = paths.where((path) => _extension(path) == 'json').toList();
    final scripts = paths.where((path) => _extension(path) == 'js').toList();
    final audio = paths
        .where((path) => _audioExtensions.contains(_extension(path)))
        .toList();
    final images = paths
        .where((path) => _imageExtensions.contains(_extension(path)))
        .toList();
    final json = jsons.firstOrNull;
    final stem = json == null ? null : _stem(json);
    final pairedIllustration =
        images.where((path) => _stem(path) == stem).firstOrNull ??
        images.firstOrNull;
    setState(() {
      _json = json ?? _json;
      _js =
          scripts.where((path) => _stem(path) == stem).firstOrNull ??
          scripts.firstOrNull ??
          _js;
      _music =
          audio.where((path) => _stem(path) == stem).firstOrNull ??
          audio.firstOrNull ??
          _music;
      _illustration = pairedIllustration ?? _illustration;
    });
  }

  void _start() {
    if (_json == null || _music == null) return;
    setState(() {
      _source = PluvioraSource.files(
        document: _json!,
        audio: _music!,
        orderingScript: _js,
        background: _illustration,
      );
    });
  }

  void _closePreview() {
    setState(() {
      _source = null;
      _controller.dispose();
      _controller = PluvioraController();
    });
  }

  @override
  Widget build(BuildContext context) {
    final source = _source;
    if (source != null) return _preview(source);
    return Scaffold(
      appBar: AppBar(title: const Text('Pluviora')),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          const Text(
            'Select preview files',
            style: TextStyle(fontSize: 24, fontWeight: FontWeight.w700),
          ),
          const SizedBox(height: 8),
          const Text(
            'JSON and audio are required. An optional same-stem JavaScript '
            'file supplies static ordering hints and is never executed.',
          ),
          const SizedBox(height: 24),
          FilledButton.icon(
            onPressed: _pickBundle,
            icon: const Icon(Icons.folder_open),
            label: const Text('Select and pair files'),
          ),
          const SizedBox(height: 16),
          _fileTile('Preview JSON *', _json, () async {
            final path = await _pickOne(const ['json']);
            if (path != null) setState(() => _json = path);
          }),
          _fileTile('Same-stem JavaScript', _js, () async {
            final path = await _pickOne(const ['js']);
            if (path != null) setState(() => _js = path);
          }),
          _fileTile('Audio *', _music, () async {
            final path = await _pickOne(_audioExtensions);
            if (path != null) setState(() => _music = path);
          }),
          _fileTile('Background AVIF / PNG / JPEG', _illustration, () async {
            final path = await _pickOne(_imageExtensions);
            if (path != null) setState(() => _illustration = path);
          }),
          const SizedBox(height: 20),
          FilledButton(
            onPressed: _json != null && _music != null ? _start : null,
            child: const Padding(
              padding: EdgeInsets.symmetric(vertical: 12),
              child: Text('Start preview'),
            ),
          ),
        ],
      ),
    );
  }

  Widget _preview(PluvioraSource source) {
    final durationMs = _controller.duration.inMilliseconds;
    final positionMs = _controller.position.inMilliseconds.clamp(0, durationMs);
    return Scaffold(
      body: Stack(
        fit: StackFit.expand,
        children: [
          PluvioraPlayer(source: source, controller: _controller),
          Positioned(
            left: 12,
            right: 12,
            bottom: 10,
            child: SafeArea(
              child: DecoratedBox(
                decoration: BoxDecoration(
                  color: Colors.black.withValues(alpha: 0.55),
                  borderRadius: BorderRadius.circular(18),
                ),
                child: Row(
                  children: [
                    IconButton(
                      onPressed: _closePreview,
                      icon: const Icon(Icons.close),
                    ),
                    IconButton(
                      onPressed:
                          _controller.loadState == PluvioraLoadState.ready
                          ? () {
                              if (_controller.playbackState ==
                                  PluvioraPlaybackState.playing) {
                                _controller.pause();
                              } else {
                                _controller.play();
                              }
                            }
                          : null,
                      icon: Icon(
                        _controller.playbackState ==
                                PluvioraPlaybackState.playing
                            ? Icons.pause
                            : Icons.play_arrow,
                      ),
                    ),
                    Expanded(
                      child: Slider(
                        value: positionMs.toDouble(),
                        max: durationMs <= 0 ? 1 : durationMs.toDouble(),
                        onChanged: durationMs <= 0
                            ? null
                            : (value) => _controller.seek(
                                Duration(milliseconds: value.round()),
                              ),
                      ),
                    ),
                    Padding(
                      padding: const EdgeInsets.only(right: 14),
                      child: Text(
                        '${_time(_controller.position)} / ${_time(_controller.duration)}',
                        style: const TextStyle(
                          fontFeatures: [FontFeature.tabularFigures()],
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _fileTile(String title, String? path, VoidCallback onTap) => Card(
    child: ListTile(
      title: Text(title),
      subtitle: Text(path == null ? 'Not selected' : _basename(path)),
      trailing: const Icon(Icons.chevron_right),
      onTap: onTap,
    ),
  );

  static String _basename(String path) =>
      path.replaceAll('\\', '/').split('/').last;
  static String _extension(String path) =>
      _basename(path).split('.').last.toLowerCase();
  static String _stem(String path) {
    final name = _basename(path);
    final dot = name.lastIndexOf('.');
    return (dot < 0 ? name : name.substring(0, dot)).toLowerCase();
  }

  static String _time(Duration value) {
    final minutes = value.inMinutes;
    final seconds = value.inSeconds.remainder(60).toString().padLeft(2, '0');
    return '$minutes:$seconds';
  }

  @override
  void dispose() {
    _refresh?.cancel();
    _controller.dispose();
    super.dispose();
  }
}
