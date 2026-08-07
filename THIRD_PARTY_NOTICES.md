# Third-party notices

## Reference renderer implementation

Portions of the native rendering behavior were adapted from an MIT-licensed
reference implementation:

Copyright (c) 2026 qaqFei

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## yyjson 0.12.0

Pluviora distributes yyjson 0.12.0 in `src/third_party/yyjson`. yyjson is
Copyright (c) 2020 Yaoyuan Guo and contributors and is licensed under the MIT
License. The complete license is in `src/third_party/yyjson/LICENSE`.

## stb_truetype 1.26

Pluviora distributes `stb_truetype.h` 1.26 in `src/third_party/stb`.
It was authored by Sean Barrett / RAD Game Tools and is offered under a choice
of the MIT License or the public domain dedication included at the end of the
header. Pluviora uses it under the MIT option.

## Flutter dependencies

Pluviora uses packages including `flutter_soloud`, `flutter_avif`, `ffi`,
`hooks`, `code_assets`, and `native_toolchain_c`. Each package remains subject
to its own license. Run `dart pub deps` to inspect the resolved dependency tree.

## Bundled runtime assets

`files/resources/runtime` contains font, image, and sound assets supplied for
the supported preview format. These assets are distributed with permission
from their copyright holder and are not licensed under Pluviora's MIT source
code license. Do not extract or redistribute them independently of Pluviora
unless you have separate permission.
