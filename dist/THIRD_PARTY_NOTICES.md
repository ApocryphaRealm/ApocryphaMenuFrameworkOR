# Third-party components and their notices

Apocrypha Menu Framework as a whole is GPL-3.0-or-later (`LICENSE`, `NOTICE.md`). These components are included under their own
GPL-compatible licences; their notices are reproduced as those licences require.

## CommonLibSSE-NG 7.2.0 - Skyrim 1.7.x build line

https://github.com/alandtse/CommonLibSSE-NG (commit 7a60f4de794095d7b0f8928d1b930a52e9a7da83), GPL-3.0-or-later WITH
Modding Exception AND GPL-3.0 Linking Exception (with Corresponding Source); the exceptions ship as
`CommonLibSSE-NG-EXCEPTIONS.md` beside the 1.7 build.

## CommonLibSSE-NG 3.7.0 - SE 1.5.97 / AE 1.6.1170 build line

MIT License

Copyright (c) 2018 Ryan-rsm-McKenzie

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## DevBench consumer API (`include/DevBench/`, `source/DevBench/`)

MIT - the notice is `include/DevBench/DevBenchAPI.LICENSE.txt`, kept with the files.

## Notes carried from the previous licence file

Third-party components, each under its own permissive licence:

* Dear ImGui (MIT) - https://github.com/ocornut/imgui
* CommonLibSSE-NG (MIT) - https://github.com/CharmedBaryon/CommonLibSSE-NG
* DevBenchAPI header/source (MIT) - the consumer API of DevBench, vendored so the framework can
  register its DevBench driving tools; devbench.dll itself is a separate, optional, GPL program
  that this framework only talks to over its REST API.

Compatibility note: Apocrypha Menu Framework exports an API compatible with the PUBLIC consumer
header of SKSE Menu Framework so that mods written against that header can register with it. It
is an original implementation and contains no code from SKSE Menu Framework.
