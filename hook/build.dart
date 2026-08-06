import 'package:hooks/hooks.dart';
import 'package:logging/logging.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

void main(List<String> args) async {
  await build(args, (input, output) async {
    final packageName = input.packageName;
    final builder = CBuilder.library(
      name: packageName,
      assetName: '${packageName}_bindings_generated.dart',
      sources: const ['src/pluviora.cpp', 'src/yyjson_bridge.cpp'],
      includes: const ['src', 'src/third_party/yyjson'],
      language: Language.cpp,
      std: 'c++20',
    );
    await builder.run(
      input: input,
      output: output,
      logger: Logger.root
        ..level = Level.ALL
        // ignore: avoid_print
        ..onRecord.listen((record) => print(record.message)),
    );
  });
}
