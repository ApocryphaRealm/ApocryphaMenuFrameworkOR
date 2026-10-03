// SPDX-License-Identifier: MIT
// Copyright (c) 2026 ApocryphaRealm
//
// ============================================================================================================
// AMF.h - the public API of the Apocrypha Menu Framework, for mods that want a settings page in its menu.
//
// One header, nothing to link. Vendor it into your plugin as-is. It works with the framework on Oblivion
// Remastered (OBSE64, ApocryphaMenuFramework.dll) and on Skyrim (SKSE, !ApocryphaMenuFramework.dll).
//
// EVERY CALL IS SAFE WITHOUT THE FRAMEWORK. Functions are looked up by name the first time they are used, and
// a missing framework or an older one without a given function gives the documented fallback (false, 0,
// nullptr or nothing) - never a crash. So the framework can be an OPTIONAL dependency: check AMF::IsInstalled()
// and carry on without a menu when it is false.
//
// ------------------------------------------------------------------------------------------------------------
// A PAGE IN FIVE LINES
//
//     #include <imgui.h>          // Dear ImGui 1.90.8 (docking branch) - see DRAWING below
//     #include "AMF.h"
//
//     static void DrawMyPage()
//     {
//         if (!AMF::UseFrameworkImGui()) return;      // draw with the framework's ImGui, every time
//         precise::SliderFloat("Speed", &g_speed, 0.5f, 3.0f, "%.2f");   // PreciseSlider.h: one unit per D-pad nudge
//         ImGui::Checkbox("Enabled", &g_enabled);
//     }
//
//     // once, after all plugins have loaded (OBSE/SKSE post-load message, or your first frame)
//     AMF::RegisterPage("My Mod", "Settings", &DrawMyPage);
//
// A mod with several pages registers each one under the same mod name; the framework shows them as tabs.
// Register whenever you like - before the game has drawn a frame is fine; the page appears when the menu opens.
//
// ------------------------------------------------------------------------------------------------------------
// DRAWING
//
// Your render callback runs inside the framework's own Dear ImGui frame, so it must draw through the
// FRAMEWORK'S ImGui context, not a copy of its own. Two ways:
//
//   1. C++ Dear ImGui (recommended). Build against Dear ImGui 1.90.8 from the docking branch, WITHOUT
//      IMGUI_DISABLE_OBSOLETE_FUNCTIONS and with the default imconfig (16-bit ImDrawIdx, void* ImTextureID).
//      With vcpkg, the stock port gives another version (1.90.2 at the usual CommonLibSSE-NG baseline), so pin
//      it with the overlay port in example/cmake/ports/imgui (1.90.8 docking) and the "docking-experimental"
//      feature, as the example does. Include <imgui.h> BEFORE this header. Call
//      AMF::UseFrameworkImGui() at the top of every callback: it checks that your ImGui matches the
//      framework's byte for byte, adopts the framework's context and allocators, and returns false (draw
//      nothing) if they differ - a mismatched ImGui would corrupt memory. (Oblivion 0.1.0+, Skyrim 1.7.2+)
//
//   2. The C exports. The framework exports the complete cimgui 1.90.8dock function set by name ("igText",
//      "igSliderFloat", "igBeginTabBar" ...). Resolve any of them with AMF::Proc("igText") and call them with
//      cimgui's signatures (https://github.com/cimgui/cimgui/tree/1.90.8dock). This works from C, from another
//      language, or from C++ built against a different ImGui.
//
// ------------------------------------------------------------------------------------------------------------
// VERSIONS
//
// AMF::APIVersion() is bumped only on a BREAKING change to anything below (it is 1). New functions are added
// without bumping it and are simply absent from older frameworks - which the fallbacks already handle. The
// per-function notes say which framework version added each one.
// ============================================================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#	define AMF_H_UNDEF_LEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#	define AMF_H_UNDEF_NOMINMAX
#endif
#include <Windows.h>
#ifdef AMF_H_UNDEF_LEAN
#	undef WIN32_LEAN_AND_MEAN
#	undef AMF_H_UNDEF_LEAN
#endif
#ifdef AMF_H_UNDEF_NOMINMAX
#	undef NOMINMAX
#	undef AMF_H_UNDEF_NOMINMAX
#endif

namespace AMF
{
	// Called by the framework, inside its frame, while your page is the one on screen.
	using RenderCallback = void (*)();

	// What the player is using right now; the framework switches by itself to whatever was last touched.
	enum class InputMode : std::uint32_t
	{
		kKeyboardMouse = 0,
		kController = 1,
	};

	namespace detail
	{
		inline HMODULE Module()
		{
			// Looked up each time until found, then remembered: a mod may ask before the framework has loaded.
			static HMODULE s_module = nullptr;
			if (!s_module) {
				s_module = ::GetModuleHandleW(L"ApocryphaMenuFramework.dll");     // Oblivion Remastered
			}
			if (!s_module) {
				s_module = ::GetModuleHandleW(L"!ApocryphaMenuFramework.dll");    // Skyrim (sorts first on purpose)
			}
			return s_module;
		}

		// One cached pointer per export; stays null (and is asked again) until the framework provides it.
		template <class Fn>
		Fn Resolve(const char* a_name, Fn& a_cache)
		{
			if (!a_cache) {
				if (HMODULE m = Module()) {
					a_cache = reinterpret_cast<Fn>(::GetProcAddress(m, a_name));
				}
			}
			return a_cache;
		}
	}

// AMF_H_FN("ExportName", <function pointer type>) declares `fn`: the export, cached, or nullptr.
#define AMF_H_FN(name, ...)                 \
	using AMF_H_Fn = __VA_ARGS__;           \
	static AMF_H_Fn s_fn = nullptr;         \
	AMF_H_Fn fn = ::AMF::detail::Resolve(name, s_fn)

	// ---- the framework ----------------------------------------------------------------------------------------

	// True when the framework is loaded in this process.
	inline bool IsInstalled() { return detail::Module() != nullptr; }

	// Any export by name, or nullptr - for the cimgui ig* functions, or anything this header does not wrap.
	inline void* Proc(const char* a_name)
	{
		HMODULE m = detail::Module();
		return m && a_name ? reinterpret_cast<void*>(::GetProcAddress(m, a_name)) : nullptr;
	}

	// 1 today; bumped only on a breaking change. 0 when the framework is not installed.
	inline std::uint32_t APIVersion()
	{
		AMF_H_FN("AMF_GetAPIVersion", std::uint32_t (*)());
		return fn ? fn() : 0u;
	}

	// The framework's own version, e.g. "0.1.0". Empty when it is not installed.
	inline const char* Version()
	{
		AMF_H_FN("AMF_GetVersionString", const char* (*)());
		const char* v = fn ? fn() : nullptr;
		return v ? v : "";
	}

	// ---- pages ------------------------------------------------------------------------------------------------

	// Adds a page to the menu under your mod's name. Returns false if the framework is absent or refused it
	// (an empty name or a null callback). Registering the same (mod, page) again replaces the callback.
	inline bool RegisterPage(const char* a_modName, const char* a_pageName, RenderCallback a_render)
	{
		AMF_H_FN("AMF_RegisterPage", bool (*)(const char*, const char*, RenderCallback));
		return fn && a_modName && a_pageName && a_render ? fn(a_modName, a_pageName, a_render) : false;
	}

	// Hides or shows one of your pages without unregistering it (e.g. an "advanced" switch). Cheap enough to call
	// every frame. False when that page is not registered. (Skyrim 1.8.3+, Oblivion 0.1.0+)
	inline bool SetPageVisible(const char* a_modName, const char* a_pageName, bool a_visible)
	{
		AMF_H_FN("AMF_SetPageVisible", bool (*)(const char*, const char*, bool));
		return fn && a_modName && a_pageName ? fn(a_modName, a_pageName, a_visible) : false;
	}

	// For a page that draws its OWN tab bar: tell the framework how many tabs and which is open, once per frame,
	// and it hands back the tab the controller's shoulder buttons asked for, or -1. Without this the D-pad and
	// bumpers cannot reach tabs the framework did not draw. (Skyrim 1.8.7+, Oblivion 0.1.0+)
	inline int DeclareInnerTabs(int a_count, int a_current)
	{
		AMF_H_FN("AMF_DeclareInnerTabs", int (*)(int, int));
		return fn ? fn(a_count, a_current) : -1;
	}

	// ---- the menu ---------------------------------------------------------------------------------------------

	// Opens the menu on your mod's page (a null or empty name opens it where it was). For a mod whose own
	// settings key should land on its page. False when that mod is not registered - the menu still opens.
	inline bool OpenMenu(const char* a_modName = nullptr)
	{
		AMF_H_FN("AMF_OpenMenu", bool (*)(const char*));
		return fn ? fn(a_modName) : false;
	}

	inline void CloseMenu()
	{
		AMF_H_FN("AMF_CloseMenu", void (*)());
		if (fn) { fn(); }
	}

	// True while the framework's window is up (1.0.5). A mod that reads the controller itself, before the framework's
	// pad gate empties the game's reads, stands down on it. False when the framework is missing or older.
	inline bool IsMenuOpen()
	{
		AMF_H_FN("AMF_IsMenuOpen", bool (*)());
		return fn ? fn() : false;
	}

	// ---- language ---------------------------------------------------------------------------------------------

	// The language the menu is showing: "english", "french", "german", "italian", "spanish", "polish",
	// "russian", "czech", "japanese", "korean" or "chinese". The pointer stays valid; compare it each frame and
	// reload your own strings when it changes, so one setting drives every page. "english" without the framework.
	inline const char* Language()
	{
		AMF_H_FN("AMF_GetLanguage", const char* (*)());
		const char* l = fn ? fn() : nullptr;
		return l ? l : "english";
	}

	// ---- input ------------------------------------------------------------------------------------------------

	// Keyboard/mouse or controller - use it to word your own button prompts.
	inline InputMode GetInputMode()
	{
		AMF_H_FN("AMF_GetInputMode", std::uint32_t (*)());
		return fn && fn() == 1u ? InputMode::kController : InputMode::kKeyboardMouse;
	}

	// The keys the framework is using right now (its menu key first, then its navigation keys), as DirectInput
	// scan codes. Refuse these in your own key-binding capture. With a null buffer, returns how many there are.
	inline std::uint32_t ReservedKeys(std::int32_t* a_buffer, std::uint32_t a_capacity)
	{
		AMF_H_FN("SMF_GetReservedKeyCodes", std::uint32_t (*)(std::int32_t*, std::uint32_t));
		return fn ? fn(a_buffer, a_capacity) : 0u;
	}

	// Both thumbsticks, apart (the framework otherwise folds them into navigation). While captured, neither
	// stick moves the menu's selection. ALWAYS release when your page is done with them - the framework also
	// releases them when its menu closes. (Skyrim 1.9.5+, Oblivion 0.1.0+)
	inline void SetSticksCaptured(bool a_captured)
	{
		AMF_H_FN("AMF_SetSticksCaptured", void (*)(bool));
		if (fn) { fn(a_captured); }
	}

	// a_which: 0 = left, 1 = right. x and y in [-1, 1], y positive up. a_clicked: L3/R3 held now. a_live: past
	// the navigation deadzone, so a resting stick reads as still. Any pointer may be null. False without the framework.
	inline bool GetStick(int a_which, float* a_x, float* a_y, bool* a_clicked = nullptr, bool* a_live = nullptr)
	{
		AMF_H_FN("AMF_GetStick", bool (*)(int, float*, float*, bool*, bool*));
		return fn ? fn(a_which, a_x, a_y, a_clicked, a_live) : false;
	}

	// ---- key capture, for a bind button (Oblivion 1.0.2+) ------------------------------------------------------
	// Arms the next press on one side as a binding. While armed, that press reaches neither the menu's navigation
	// nor the game - so a controller player can bind B or A without the page backing out or activating. Escape
	// cancels a keyboard capture; the controller side has no cancel button (every button is a valid binding), so
	// give the page a Cancel button the mouse can click and rely on the timeout. The other side's input is left
	// alone.
	enum class CaptureState : std::int32_t { kIdle = 0, kWaiting = 1, kCaptured = 2, kCancelled = 3, kTimedOut = 4 };
	// What was captured, when the state is kCaptured:
	//   kind 0 keyboard   code = DirectInput scan code
	//   kind 1 mouse      code = 0 left, 1 right, 2 middle, 3 thumb 1, 4 thumb 2
	//   kind 2 pad        code = XInput button mask (0x1000 A, 0x2000 B, 0x0100 left shoulder, ...)
	//   kind 3 stick      code = (stick << 4) | dir; stick 0 left, 1 right; dir 0 up, 1 down, 2 left, 3 right
	//   kind 4 trigger    code = 0 left, 1 right
	//   kind 5 wheel      code = 0 up, 1 down
	inline void BeginKeyCapture(bool a_gamepadSide, std::int32_t a_timeoutMs = 8000)
	{
		AMF_H_FN("AMF_BeginKeyCapture", void (*)(bool, std::int32_t));
		if (fn) { fn(a_gamepadSide, a_timeoutMs); }
	}

	inline void CancelKeyCapture()
	{
		AMF_H_FN("AMF_CancelKeyCapture", void (*)());
		if (fn) { fn(); }
	}

	// Poll once a frame while waiting. A finished capture (captured / cancelled / timed out) is reported once, then
	// the state returns to idle. kIdle without the framework (or with one older than 1.0.2).
	inline CaptureState PollKeyCapture(std::int32_t* a_kind, std::int32_t* a_code)
	{
		AMF_H_FN("AMF_PollKeyCapture", std::int32_t (*)(std::int32_t*, std::int32_t*));
		return fn ? static_cast<CaptureState>(fn(a_kind, a_code)) : CaptureState::kIdle;
	}

	// True when this framework has the capture calls (false on older ones - offer no bind button then).
	inline bool HasKeyCapture() { return Proc("AMF_BeginKeyCapture") != nullptr; }

	// The controller's on-screen keyboard for the text box the highlight is on (it also opens by itself when A
	// is pressed on a text box). (Skyrim 1.8.9+, Oblivion 0.1.0+)
	inline void ShowKeyboard()
	{
		AMF_H_FN("AMF_ShowKeyboard", void (*)());
		if (fn) { fn(); }
	}

	inline void HideKeyboard()
	{
		AMF_H_FN("AMF_HideKeyboard", void (*)());
		if (fn) { fn(); }
	}

	// ---- theme ------------------------------------------------------------------------------------------------

	// Draws the active theme's frame around a rectangle of your own (display pixels) on an ImDrawList - so a box
	// your mod floats over the game matches the menu. False when the theme has no frame art; draw your own line
	// then. (Skyrim 1.8.9+, Oblivion 0.1.0+)
	inline bool DrawThemeFrame(void* a_imDrawList, float a_x0, float a_y0, float a_x1, float a_y1)
	{
		AMF_H_FN("AMF_DrawThemeFrame", bool (*)(void*, float, float, float, float));
		return fn && a_imDrawList ? fn(a_imDrawList, a_x0, a_y0, a_x1, a_y1) : false;
	}

	// ---- Dear ImGui -------------------------------------------------------------------------------------------

	// The framework's ImGuiContext* (null until the game has drawn its first frame). UseFrameworkImGui() is the
	// normal way to use it. (Oblivion 0.1.0+; Skyrim 1.7.2+, through its cimgui igGetCurrentContext export)
	inline void* ImGuiContext()
	{
		AMF_H_FN("AMF_GetImGuiContext", void* (*)());
		if (fn) {
			return fn();
		}
		static void* (*s_cimgui)() = nullptr;
		return detail::Resolve("igGetCurrentContext", s_cimgui) ? s_cimgui() : nullptr;
	}

#ifdef IMGUI_VERSION
	// Call at the top of EVERY render callback, and draw nothing when it returns false. The first call checks
	// that your Dear ImGui is the framework's (same version and data layout) and adopts its allocators; each call
	// makes the framework's context current in your module. Returns false - for good - when the builds differ,
	// and until then whenever the framework or its context is not there.
	inline bool UseFrameworkImGui()
	{
		static int s_state = 0;   // 0 not yet checked, 1 compatible, -1 refused
		if (s_state < 0) {
			return false;
		}
		if (s_state == 0) {
			using CheckFn = bool (*)(const char*, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t, std::size_t);
			AMF_H_FN("AMF_CheckImGuiABI", CheckFn);
			void* allocFn = nullptr;
			void* freeFn = nullptr;
			void* user = nullptr;
			if (fn) {
				// Oblivion: the framework's own check and allocators
				if (!fn(IMGUI_VERSION, sizeof(ImGuiIO), sizeof(ImGuiStyle), sizeof(ImVec2), sizeof(ImVec4), sizeof(ImDrawVert), sizeof(ImDrawIdx))) {
					s_state = -1;   // the framework logs the mismatch; use the ig* exports (AMF::Proc) instead
					return false;
				}
				static bool (*s_alloc)(void**, void**, void**) = nullptr;
				if (!(detail::Resolve("AMF_GetImGuiAllocatorFunctions", s_alloc) && s_alloc(&allocFn, &freeFn, &user))) {
					allocFn = freeFn = nullptr;
				}
			} else {
				// Skyrim: the same answers from the framework's cimgui exports - its Dear ImGui's own version string,
				// its own layout check (ImGui's IMGUI_CHECKVERSION, made inside the framework) and its allocators
				static const char* (*s_version)() = nullptr;
				static CheckFn s_layout = nullptr;
				static void (*s_cimguiAlloc)(void**, void**, void**) = nullptr;
				if (!detail::Resolve("igGetVersion", s_version) || !detail::Resolve("igDebugCheckVersionAndDataLayout", s_layout) ||
					!detail::Resolve("igGetAllocatorFunctions", s_cimguiAlloc)) {
					return false;   // no framework yet, or one too old to share its context - try again next call
				}
				const char* theirs = s_version();
				const char* ours = IMGUI_VERSION;
				std::size_t i = 0;
				while (theirs && theirs[i] && theirs[i] == ours[i]) {
					++i;
				}
				// the version first, so the layout check (which asserts in a debug ImGui) only ever sees a matching build
				if (!theirs || theirs[i] != ours[i] ||
					!s_layout(IMGUI_VERSION, sizeof(ImGuiIO), sizeof(ImGuiStyle), sizeof(ImVec2), sizeof(ImVec4), sizeof(ImDrawVert), sizeof(ImDrawIdx))) {
					s_state = -1;   // a different Dear ImGui: use the ig* exports (AMF::Proc) instead
					return false;
				}
				s_cimguiAlloc(&allocFn, &freeFn, &user);
			}
			if (allocFn && freeFn) {
				ImGui::SetAllocatorFunctions(reinterpret_cast<ImGuiMemAllocFunc>(allocFn), reinterpret_cast<ImGuiMemFreeFunc>(freeFn), user);
			}
			s_state = 1;
		}
		void* ctx = ImGuiContext();
		if (!ctx) {
			return false;
		}
		ImGui::SetCurrentContext(static_cast<::ImGuiContext*>(ctx));
		return true;
	}
#endif

#undef AMF_H_FN
}
