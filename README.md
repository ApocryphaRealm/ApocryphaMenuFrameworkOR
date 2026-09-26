# Apocrypha Menu Framework for Oblivion Remastered

One in-game settings menu for every mod - the Apocrypha Menu Framework, on The Elder Scrolls IV: Oblivion Remastered.
An OBSE64 plugin that draws Dear ImGui over the game's DirectX 12 renderer, with the same menu, themes, controls and mod
API as the [Skyrim framework](https://github.com/ApocryphaRealm/ApocryphaMenuFramework).

* What the player gets and where the files go: `dist/README.txt`
* What changed: `CHANGELOG.md`

## For mod authors

`sdk/include/AMF.h` is the whole public API: one header, nothing to link, safe when the framework is not installed.
`sdk/example/main.cpp` is a complete mod with two pages built against it. Draw with the ordinary C++ Dear ImGui API
(1.90.8, docking branch) through the framework's context, or with the cimgui `ig*` functions the framework exports.

## Building

* [xmake](https://xmake.io) 3.0+, a C++23 compiler (MSVC), and the submodule: `git clone --recurse-submodules`.
* `xmake build ApocryphaMenuFramework` (the framework) and `xmake build AMFExample` (the SDK sample). Dear ImGui is
  vendored under `extern/imgui`; MinHook comes from xmake's package registry.
* The plugin goes to `OblivionRemastered\Binaries\Win64\OBSE\Plugins\` beside the game executable (with Mod Organizer
  2, the Root Builder layout) together with `dist/OBSE/Plugins/*`.

## Licence

GPL-3.0-or-later (`LICENSE`, `dist/NOTICE.md`); third-party notices in `dist/THIRD_PARTY_NOTICES.md`. `sdk/` is MIT so
any mod can vendor it.
