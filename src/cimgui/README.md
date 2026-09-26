# cimgui, vendored

`cimgui.h` here is **unmodified**, taken from <https://github.com/cimgui/cimgui> at tag
**`1.90.8dock`**, MIT licensed.

`cimgui.cpp` here is **generated, not edited** - `tools/harden-cimgui.py` reads the pristine
upstream copy kept at `extern/cimgui-1.90.8dock/cimgui.cpp` and writes this one, adding null guards
and changing nothing else. The pristine copy lives outside `source/` on purpose: `CMakeLists.txt`
globs `source/*.cpp`, so keeping it there would compile both and collide on every symbol.

**Why the guards.** A consumer reaches these functions through raw pointers it resolved by name, so
nothing validates what it passes; the wrappers this framework used to hand-write checked every
pointer and string, and cimgui's do not. The generator's docstring says exactly what is guarded and
what is deliberately left alone - notably ImGui's optional `p_*` pointers, because guarding
`igBegin`'s `p_open` would stop every window without a close button from drawing.

## Why they are in this repository

A mod written for SKSE Menu Framework does not link against a framework - it resolves every drawing
function it uses by name at runtime. That name set *is* cimgui: SMF re-exports the generated cimgui
wrappers, and a consumer calls them through `GetProcAddress`. A name the framework does not export
comes back null and that part of the consumer's page silently draws nothing, which is what a user
reported on 2026-09-10 as "settings show up as blank".

This framework used to hand-write those wrappers, and covered 252 of SMF's 1,420 names across four
tranches, each chosen by scanning whichever mods happened to be installed. Vendoring the generated
source ends that: every name exists, with the signature it is supposed to have, correct by
construction rather than by eye.

## Why this exact tag

`1.90.8dock` matches the Dear ImGui this framework embeds on both build lines - 1.90.8 with
vcpkg's `docking-experimental` feature - and it is the one that lines up with SMF:

| cimgui tag | declares | in SMF's export table |
| --- | --- | --- |
| **1.90.8dock** | **1,397** | **1,397 - all of them** |
| 1.90.8 (no docking) | 1,304 | 1,304 |
| docking_inter | 1,615 | 1,320 |

The 23 SMF exports cimgui does not provide are SMF's own API - `AddSectionItem`, `LoadTexture`,
`SKSEPlugin_*` and so on - and live in `source/Compat.cpp`.

`CIMGUI_API` already expands to `extern "C" __declspec(dllexport)`, so compiling this file is the
whole of the integration; `CMakeLists.txt` globs `source/*.cpp`.

## If Dear ImGui is ever upgraded

Drop the new `cimgui.cpp`/`cimgui.h` into `extern/<tag>/`, point `SOURCE` in
`tools/harden-cimgui.py` at it, run the script, and copy the new header here. Because the compiled
file is generated rather than patched, there is no diff to re-apply and no merge to get wrong.

Re-fetch cimgui at the matching tag. A mismatch between this file and the embedded imgui headers is
a compile error, not a silent break, which is the point of vendoring the generated source rather
than transcribing it.
