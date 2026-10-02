// Apocrypha Menu Framework - Oblivion Remastered (OBSE64). Copyright (C) 2026 ApocryphaRealm. GPL-3.0-or-later.
//
// The public contract (include/AMF/API.h) is the Skyrim AMF's, unchanged: a mod written against it registers its
// pages here the same way. The SKSE-only parts of the Skyrim entry point (the SKSEMenuFramework.dll alias, the
// Address Library pre-check, the SKSE message listener) have no Oblivion counterpart and are not carried over.

#include "AMF/API.h"
#include "Bindings.h"
#include "Input.h"
#include "Keyboard.h"
#include "Overlay.h"
#include "Registry.h"
#include "Renderer.h"
#include "Settings.h"
#include "Strings.h"
#include "Pause.h"
#include "Screenshot.h"
#include "SystemRow.h"
#include "Tick.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>

namespace
{
	// The keys the framework consumes while its menu is open, as DirectInput scan codes (the numbering the whole
	// core uses): Tab, Escape, the arrows, Enter. The MENU key is not in this list - SMF_GetReservedKeyCodes puts the
	// LIVE settings::Get().toggleKey first at every call, so a key the player moved AMF away from is free again.
	constexpr std::array<std::int32_t, 7> kNavigationKeys{ 0x0F, 0x01, 0xC8, 0xD0, 0xCB, 0xCD, 0x1C };
}

AMF_API std::uint32_t SMF_GetReservedKeyCodes(std::int32_t* a_buffer, std::uint32_t a_capacity)
{
	std::array<std::int32_t, kNavigationKeys.size() + 1> reserved{};
	std::uint32_t count = 0;
	const std::int32_t live = settings::Get().toggleKey;
	if (live > 0) {
		reserved[count++] = live;
	}
	for (const std::int32_t nav : kNavigationKeys) {
		if (nav != live) {
			reserved[count++] = nav;
		}
	}
	if (!a_buffer) {
		return count;   // null buffer = "how big a buffer do I need"
	}
	const std::uint32_t written = a_capacity < count ? a_capacity : count;
	for (std::uint32_t i = 0; i < written; ++i) {
		a_buffer[i] = reserved[i];
	}
	logger::debug("SMF_GetReservedKeyCodes: reported {} reserved key(s) to a caller (menu key 0x{:X} first)", written, live);
	return written;
}

AMF_API const char* AMF_GetVersionString()
{
	return AMF_VERSION;
}

AMF_API std::uint32_t AMF_GetAPIVersion()
{
	return 1;
}

AMF_API bool AMF_RegisterPage(const char* a_modName, const char* a_pageName, AMF_RenderCallback a_render)
{
	return registry::Register(a_modName, a_pageName, a_render);
}

AMF_API const char* AMF_GetLanguage()
{
	// A process-lifetime buffer: consumers may keep the pointer and compare each frame.
	static char s_buffer[64] = "english";
	const std::string& lang = strings::Language();
	if (std::strncmp(s_buffer, lang.c_str(), sizeof(s_buffer)) != 0) {
		std::snprintf(s_buffer, sizeof(s_buffer), "%s", lang.c_str());
	}
	return s_buffer;
}

AMF_API bool AMF_OpenMenu(const char* a_modName)
{
	// A consumer's own settings key opens the framework ON its page; an unknown or empty name opens the menu where
	// it last was. Returns whether the named mod was found.
	renderer::SetMenuVisible(true, false);
	if (!a_modName || !*a_modName) {
		return true;
	}
	const std::vector<registry::Entry> entries = registry::Snapshot();
	for (std::size_t i = 0; i < entries.size(); ++i) {
		if (entries[i].modName == a_modName) {
			renderer::SetSelectedNode("mod:" + std::to_string(i));
			logger::info("AMF_OpenMenu: opened on '{}' (registry index {})", a_modName, i);
			return true;
		}
	}
	logger::info("AMF_OpenMenu: '{}' is not registered; menu opened where it was", a_modName);
	return false;
}

AMF_API void AMF_CloseMenu()
{
	renderer::SetMenuVisible(false, false);
}

// 1.0.5: whether the framework's window is up - a mod that reads the controller itself (before the pad gate empties
// the game's reads) stands down on it while the menu is open.
AMF_API bool AMF_IsMenuOpen()
{
	return renderer::IsMainWindowVisible();
}

AMF_API bool AMF_DrawThemeFrame(void* a_drawList, float a_x0, float a_y0, float a_x1, float a_y1)
{
	return renderer::DrawThemeFrameAround(static_cast<ImDrawList*>(a_drawList), a_x0, a_y0, a_x1, a_y1);
}

AMF_API void AMF_ShowKeyboard()
{
	keyboard::Show();
}

AMF_API void AMF_HideKeyboard()
{
	keyboard::Hide();
}

AMF_API void AMF_SetSticksCaptured(bool a_captured)
{
	input::SetSticksCaptured(a_captured);
}

AMF_API bool AMF_GetStick(int a_which, float* a_x, float* a_y, bool* a_clicked, bool* a_live)
{
	float x = 0.0f, y = 0.0f;
	bool  clicked = false, live = false;
	input::GetStick(a_which, x, y, clicked, live);
	if (a_x) { *a_x = x; }
	if (a_y) { *a_y = y; }
	if (a_clicked) { *a_clicked = clicked; }
	if (a_live) { *a_live = live; }
	return true;
}

// A consumer mod's bind button (2026-09-28, Ultimate Combat's rebuild): the next press on that side is recorded
// and swallowed, so the menu's own navigation does not act on it (Bindings.h, the consumer capture).
AMF_API void AMF_BeginKeyCapture(bool a_gamepadSide, std::int32_t a_timeoutMs)
{
	bindings::BeginConsumerCapture(a_gamepadSide, a_timeoutMs);
}

AMF_API void AMF_CancelKeyCapture()
{
	bindings::CancelConsumerCapture();
}

AMF_API std::int32_t AMF_PollKeyCapture(std::int32_t* a_kind, std::int32_t* a_code)
{
	return static_cast<std::int32_t>(bindings::PollConsumerCapture(a_kind, a_code));
}

AMF_API bool AMF_SetPageVisible(const char* a_modName, const char* a_pageName, bool a_visible)
{
	return registry::SetPageVisible(a_modName, a_pageName, a_visible);
}

AMF_API int AMF_DeclareInnerTabs(int a_count, int a_current)
{
	return renderer::DeclareInnerTabs(a_count, a_current);
}

AMF_API std::uint32_t AMF_GetInputMode()
{
	return input::UsingController() ? 1u : 0u;
}

// ---- Sharing the framework's Dear ImGui with a C++ consumer (Oblivion Remastered 0.1.0) ----------------------------
// A page draws inside the framework's frame, so it must use the framework's context. A consumer compiled against the
// same Dear ImGui (1.90.8 docking, obsolete functions kept) calls ImGui::SetCurrentContext / SetAllocatorFunctions
// with these and then uses the ordinary C++ API; AMF.h does it for them after AMF_CheckImGuiABI agrees. The cimgui
// ig* exports remain for C consumers and for anything built against a different ImGui.

AMF_API void* AMF_GetImGuiContext()
{
	return ImGui::GetCurrentContext();   // null until the renderer is up (first Present)
}

AMF_API bool AMF_GetImGuiAllocatorFunctions(void** a_alloc, void** a_free, void** a_userData)
{
	ImGuiMemAllocFunc alloc = nullptr;
	ImGuiMemFreeFunc  free = nullptr;
	void*             user = nullptr;
	ImGui::GetAllocatorFunctions(&alloc, &free, &user);
	if (a_alloc) { *a_alloc = reinterpret_cast<void*>(alloc); }
	if (a_free) { *a_free = reinterpret_cast<void*>(free); }
	if (a_userData) { *a_userData = user; }
	return alloc && free;
}

// The same layout test ImGui's own IMGUI_CHECKVERSION makes, answered for the framework's build: a consumer passes its
// IMGUI_VERSION and sizeof values; any difference means sharing the context would corrupt memory, so it must not.
AMF_API bool AMF_CheckImGuiABI(const char* a_version, std::size_t a_io, std::size_t a_style, std::size_t a_vec2,
	std::size_t a_vec4, std::size_t a_drawVert, std::size_t a_drawIdx)
{
	const bool ok = a_version && std::strcmp(a_version, IMGUI_VERSION) == 0 && a_io == sizeof(ImGuiIO) &&
	                a_style == sizeof(ImGuiStyle) && a_vec2 == sizeof(ImVec2) && a_vec4 == sizeof(ImVec4) &&
	                a_drawVert == sizeof(ImDrawVert) && a_drawIdx == sizeof(ImDrawIdx);
	if (!ok) {
		logger::warn("AMF_CheckImGuiABI: a consumer built against Dear ImGui \"{}\" (io {}, style {}) asked to share the "
					 "framework's {} (io {}, style {}) - refused; it must use the ig* exports instead",
			a_version ? a_version : "(null)", a_io, a_style, IMGUI_VERSION, sizeof(ImGuiIO), sizeof(ImGuiStyle));
	}
	return ok;
}

namespace
{
	// the game thread, every frame (Tick.cpp): the System-menu row and the pause, both 1.0.4
	void OnFrame()
	{
		systemrow::Tick();
		pause::Tick();
		screenshot::Tick();
	}
}

OBSE_PLUGIN_LOAD(const OBSE::LoadInterface* a_obse)
{
	OBSE::Init(a_obse);
	logger::info("Apocrypha Menu Framework {} loading (Oblivion Remastered, OBSE64)", AMF_VERSION);
	logger::info("Original framework embedding Dear ImGui (MIT); the same core and API as the Skyrim AMF");

	// Settings first: the log level (trace by default) and the menu key are read before anything else logs or binds.
	settings::Load();
	strings::Load();
	input::Install();
	// the game thread's frame tick: the System-menu row is added to the live System page from it (1.0.4)
	tick::Install(&OnFrame);

	// OBSE64 runs Load before the game's WinMain, so the renderer does not exist yet: hook the DXGI factory
	// exports now and pick up the swap chain and its command queue when the game creates them.
	if (!Overlay::Install()) {
		logger::error("the overlay hooks could not be installed; AMF will not draw");
	}
	logger::info("Successfully loaded!");
	return true;
}
