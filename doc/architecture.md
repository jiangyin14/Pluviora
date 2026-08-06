# Pluviora 架构与 ABI v1

## 分层

| 层 | 实现 | 职责 |
| --- | --- | --- |
| 原生核心 | C++20、yyjson 0.12.0 | 文档解析、动画索引、时间推进、几何、裁剪、确定性特效 |
| ABI | `src/pluviora.h` | 独立实例、状态码、元数据、警告、动态帧视图 |
| Dart 引擎 | `PluvioraEngine` | 输入内存管理、异常映射、零拷贝帧视图 |
| 播放器 | `PluvioraPlayer` | flutter_soloud 主时钟、display-driven ticker、生命周期 |
| 绘制 | `CustomPainter` | 移动图集、顶点四边形、文字、Storyboard、命中 Shader |

原生核心没有文件系统、图片、音频、OpenGL、GLFW 或桌面窗口依赖。路径和字节读取由 Dart 负责，音频播放位置是唯一时间源。

## 帧缓冲区

ABI 版本固定为 `1`，要求小端平台和 4 字节对齐。`PluvioraFrameView.data` 指向实例内部的动态 `std::vector<uint8_t>`；缓冲区按实际文档复杂度扩容，没有固定命令上限。

每条指令以 8 字节头开始：

| 偏移 | 类型 | 含义 |
| --- | --- | --- |
| 0 | `uint16` | 指令类型 |
| 2 | `uint16` | flags |
| 4 | `uint32` | 指令头和有效 payload 的总长度 |

读取方按 `align4(offset + length)` 查找下一条指令，因此未来可以通过长度跳过未知类型。当前类型包括矩形、四边形、图集精灵、音符、文字、Storyboard 图片、命中圆环和粒子。音效触发次数位于帧视图头部，不混入几何命令。

缓冲区有效期截止到同一实例下一次 `pluviora_render`、`pluviora_load` 或 `pluviora_destroy`。`PluvioraFrameView.bytes` 是原生内存视图，不做整块 Dart 复制。

## 实例与错误

- `pluviora_create` 返回独立实例句柄，不使用全局播放器单例。
- 所有导出函数校验句柄与参数。
- C++ 异常不会跨越 ABI；错误会转换为 `PluvioraStatus`，详情由 `pluviora_last_error` 返回。
- Dart 层的 `dispose` 幂等，只会调用一次原生销毁。

## 热路径

- 每个预览对象持有动画组的直接下标；逐帧不做对象属性 `unordered_map` 查询。
- 每种属性的事件按时间排序并保留前向游标，反向跳转时安全回绕。
- Hold 粒子只生成 `[当前时间 - 0.5s, 当前时间]` 的活动窗口。
- 随机参数由文档内容 FNV-1a 64 位哈希和对象索引混合生成，跨平台可复现。
- Dart 普通音符使用可复用 TypedData 和 `drawRawAtlas`；轨道与四角图片使用 `Vertices.raw`。
