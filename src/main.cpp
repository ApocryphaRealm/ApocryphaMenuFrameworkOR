// Apocrypha Menu Framework - Oblivion Remastered (OBSE64). Copyright (C) 2026 ApocryphaRealm. GPL-3.0-or-later.
//
// The public contract (include/AMF/API.h) is the Skyrim AMF's, unchanged: a mod written against it registers its
// pages here the same way. The SKSE-only parts of the Skyrim entry point (the SKSEMenuFramework.dll alias, the
// Address Library pre-check, the SKSE message listener) have no Oblivion counterpart and are not carried over.

#include "AMF/API.h"
#include "Input.h"
#include "Keyboard.h"
#include "Overlay.h"
#include "Registry.h"
#include "Renderer.h"
#include "Settings.h"
#include "Strings.h"

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

OBSE_PLUGIN_LOAD(const OBSE::LoadInterface* a_obse)
{
	OBSE::Init(a_obse);
	logger::info("Apocrypha Menu Framework {} loading (Oblivion Remastered, OBSE64)", AMF_VERSION);
	logger::info("Original framework embedding Dear ImGui (MIT); the same core and API as the Skyrim AMF");

	// Settings first: the log level (trace by default) and the menu key are read before anything else logs or binds.
	settings::Load();
	strings::Load();
	input::Install();

	// OBSE64 runs Load before the game's WinMain, so the renderer does not exist yet: hook the DXGI factory
	// exports now and pick up the swap chain and its command queue when the game creates them.
	if (!Overlay::Install()) {
		logger::error("the overlay hooks could not be installed; AMF will not draw");
	}
	logger::info("Successfully loaded!");
	return true;
}
