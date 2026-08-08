# Pluviora architecture and ABI v1

## Layers

| Layer | Implementation | Responsibility |
| --- | --- | --- |
| Native core | C++20, yyjson 0.12.0, stb_truetype 1.26 | Document parsing, animation state, geometry, clipping, effects, text rasterization |
| C ABI | `src/pluviora.h` | Independent handles, status codes, metadata, warnings, dynamic frame views |
| Dart engine | `PluvioraEngine` | Input memory, resource initialization, exception mapping, zero-copy views |
| Player | `PluvioraPlayer` | Local timestamp clock, audio synchronization, display ticker, lifecycle |
| Painter | `CustomPainter` | Full-resolution textures, quads, bitmap text, hit shader, background mask |

The native core has no filesystem, image, audio, OpenGL, GLFW, or desktop
window dependency. Dart reads paths or bytes and uses `DateTime.timestamp()` as
the playback clock, with a monotonic elapsed-time guard against system clock
corrections. Audio is synchronized only at explicit state boundaries.

## Parsing compatibility

The runtime parser consumes the JSON document directly. A companion JavaScript
file is never evaluated; a tokenizer only scans literal `n(<line>, ...)` calls
outside comments and strings. A complete sequence restores missing note
indices. Otherwise the parser assigns indices in JSON traversal order and
returns a warning.

BPM references normally use an array index. Out-of-range references fall back
to matching the numeric BPM value; the first match wins when values are
duplicated. Animation entries without a non-null `i1` target are skipped and
reported as one aggregate warning. This prevents an incomplete animation from
being attached to an unrelated object.

## Reference behavior

The core retains the reference renderer's event ordering, easing tables,
observable integration, note grouping, rewind state, clipping tests, draw
order, score/combo animation, full-resolution texture geometry, pre-generated
particle model, and process-randomized effect values. Random particle and ring
parameters can therefore differ between engine loads.

Text uses the reference 48-pixel bucket rasterization algorithm backed by
`stb_truetype`. Alpha bitmaps are run-length encoded in native memory and drawn
as cached Flutter vertex meshes. Picture storyboard entries remain no-ops;
text entries are emitted in background, normal, or foreground layers.

## Frame buffer

ABI version `1` requires a little-endian platform and 4-byte alignment.
`PluvioraFrameView.data` points to a dynamic `std::vector<uint8_t>` owned by the
engine instance. The vector grows with actual document complexity and has no
fixed command limit.

Every command starts with an 8-byte header:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | `uint16` | Command type |
| 2 | `uint16` | Flags |
| 4 | `uint32` | Header plus payload length |

Readers advance to `align4(offset + length)`, so a future reader can skip an
unknown command. Current commands cover rectangles, quads, fixed sprites,
three-part note textures, hit rings, particles, and RLE bitmap text. Sound
effect counts are stored in the frame view header rather than the geometry
stream.

The buffer is valid until the same instance next calls `pluviora_render`,
`pluviora_load`, or `pluviora_destroy`. Dart exposes a native-memory view and
does not copy the complete frame buffer.

## Instances and errors

- `pluviora_create` returns an independent engine handle.
- Every exported function validates handles and arguments.
- C++ exceptions are caught at the ABI boundary and converted to a
  `PluvioraStatus`; details are available from `pluviora_last_error`.
- Dart `dispose` and controller release paths are idempotent.

## Render path

- Animation events are sorted once and use forward cursors; seeking backward
  resets the affected cursors and hit-sound state.
- Notes are grouped by their invariant animation values and traversed in the
  reference `Hold → Tap → Drag` order.
- Hold particles are pre-generated at 0.01-second intervals, while other notes
  receive ten particles per hit effect.
- Track sprites and notes use bundled, original-dimension textures with the
  source geometry constants and three-slice hold rendering.
- UI and storyboard text are rasterized by C++ and cached as vertex meshes in
  Dart; Flutter `TextPainter` is not part of the render path.
- `CustomPainter(repaint: ...)` updates the canvas without rebuilding or
  relaying out the widget tree each frame.
