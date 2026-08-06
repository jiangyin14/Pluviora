# 第三方许可说明

## yyjson 0.12.0

Pluviora 在 `src/third_party/yyjson` 中分发 yyjson 0.12.0 源码。yyjson 由 Yaoyuan Guo 及贡献者开发，采用 MIT License；许可全文见 `src/third_party/yyjson/LICENSE`。

## Flutter 依赖

Pluviora 通过 pub 使用 `flutter_soloud`、`flutter_avif`、`ffi`、`hooks`、`code_assets`、`native_toolchain_c` 等依赖。它们仍分别适用各自随包发布的许可证；`dart pub deps` 可用于查看最终依赖树。

## 内置预览素材

`files/resources/default` 中的中性图集和提示音由本仓库的 `tool/build_assets.py` 从零生成，随项目代码一同采用 MIT License，不包含外部品牌素材。
