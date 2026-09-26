-- Apocrypha Menu Framework for The Elder Scrolls IV: Oblivion Remastered (OBSE64 plugin).
-- Its own project and repository beside the Skyrim AMF; the name stays AMF (the owner, 2026-09-26).
includes("lib/commonlibob64")

set_project("ApocryphaMenuFramework")
set_version("0.1.0")
set_license("GPL-3.0-or-later")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

add_requires("minhook")

-- Dear ImGui 1.90.8 docking - the version Skyrim AMF embeds and the tag the vendored cimgui (src/cimgui) was
-- generated for, so the ig* export surface consumers resolve by name carries over unchanged. Built WITHOUT
-- IMGUI_DISABLE_OBSOLETE_FUNCTIONS, as vcpkg's port is on Skyrim: that define removes exports and changes struct layout.
-- IMGUI_IMPL_WIN32_DISABLE_GAMEPAD: the framework reads XInput itself (Input.cpp), and the backend polling it too
-- would hand ImGui every real press twice (logic library: "a REAL controller reaches ImGui twice").
target("imgui")
    set_kind("static")
    set_warnings("none")
    add_files("extern/imgui/imgui.cpp", "extern/imgui/imgui_draw.cpp", "extern/imgui/imgui_tables.cpp", "extern/imgui/imgui_demo.cpp",
              "extern/imgui/imgui_widgets.cpp", "extern/imgui/backends/imgui_impl_dx12.cpp",
              "extern/imgui/backends/imgui_impl_win32.cpp")
    add_includedirs("extern/imgui", "extern/imgui/backends", {public = true})
    add_defines("IMGUI_IMPL_WIN32_DISABLE_GAMEPAD")

target("ApocryphaMenuFramework")
    add_rules("commonlibob64.plugin", {
        name = "ApocryphaMenuFramework",
        author = "ApocryphaRealm",
        description = "Apocrypha Menu Framework - one in-game settings menu for every mod (Oblivion Remastered)"
    })
    add_deps("imgui")
    add_packages("minhook")
    add_syslinks("d3d12", "dxgi", "user32", "ole32", "shell32", "windowscodecs")
    on_load(function (target)
        target:add("defines", "AMF_VERSION=\"" .. (target:version() or "0.0.0") .. "\"")
    end)
    add_files("src/**.cpp")
    add_headerfiles("src/**.h", "include/**.h")
    add_includedirs("include", "src")
    set_pcxxheader("src/pch.h")
