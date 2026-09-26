#pragma once

// ============================================================================================
// CONSUMER SURFACE - the half of the SMF-compatible API that is NOT a settings page.
//
// AddSectionItem covers a mod that wants a page inside the framework's own menu. The stock
// consumer header exposes a second, larger surface for mods that want to draw their own thing:
// standalone windows, HUD elements drawn over the game, named fonts, and image textures. AMF
// exported none of it, and the silence is the problem - the header's wrappers are
//
//     static auto func = GetFunction<...>("AddWindow");
//     if (func) { ... }
//
// with no else, so a missing export is a NO-OP, not an error. Measured 2026-09-04: with the
// module-name alias in place, five third-party mods resolved AMF correctly and still registered
// nothing, because what they actually call is AddWindow, not AddSectionItem.
//
// Everything here is owned by the render thread except the registries, which are guarded because
// registration happens on whatever thread a consumer's SKSEPlugin_Load or message handler runs on.
// ============================================================================================

#include <cstdint>
// For WindowStates()'s diagnostic snapshot below - it returns a vector of copied structs, one of
// which holds the window's view name.
#include <string>
#include <vector>

struct ImVec2;

namespace consumer
{
	using RenderFunction = void (*)();
	using HudCallback = void (*)();

	// The object a consumer is handed by AddWindow/AddWindowWithView. Its LAYOUT is the contract
	// (two atomics, in this order, nothing before them) because the consumer writes IsOpen
	// directly - the same arrangement GetMainWindow already ships. Allocated once and never
	// freed: a consumer keeps the pointer for the life of the process.
	struct WindowInterface
	{
		std::atomic<bool> IsOpen{ false };
		std::atomic<bool> BlockUserInput{ true };
	};

	// a_view is the optional "view name" of AddWindowWithView - kept with the entry so a window
	// can be addressed by name later; nullptr for a plain AddWindow.
	WindowInterface* AddWindow(RenderFunction a_render, const char* a_view);

	std::int64_t RegisterHudElement(HudCallback a_callback);
	void UnregisterHudElement(std::int64_t a_id);

	// Render thread, inside a frame. Windows self-gate on IsOpen; HUD elements always draw,
	// which is the whole point of being a HUD element rather than a page.
	void DrawWindows();
	void DrawHudElements();

	// True when a consumer window is up AND taking input - folded into the framework's own
	// IsAnyBlockingWindowOpened answer so a launcher gets one truthful answer for the process.
	bool AnyBlockingWindowOpen();

	// ---- fonts ----------------------------------------------------------------------------
	// PushFont(name) and the three family pushes are always balanced by Pop(): an unknown name
	// pushes the CURRENT font rather than nothing, because a consumer that pushed and popped
	// symmetrically must not be able to unbalance ImGui's stack through us.
	void PushNamedFont(const char* a_name);
	void PushRegular();
	void PushSolid();
	void PushBrands();
	void PopFont();

	// ---- textures ---------------------------------------------------------------------------
	// Cached by path: a consumer calling LoadTexture every frame (they do) must not re-decode.
	void* LoadTexture(const char* a_path, ImVec2* a_outSize);
	void DisposeTexture(const char* a_path);


	// Counts for the DevBench tool, so a live run can be checked without reading the log.
	std::size_t WindowCount();
	std::size_t HudElementCount();
	std::size_t TextureCount();

	// One consumer window's flags, copied out. DIAGNOSTIC, added 2026-09-12.
	//
	// IsAnyBlockingWindowOpened() answers `IsMainWindowVisible() || AnyBlockingWindowOpen()`, and
	// mods gate their own hotkeys on it - Gear Toggle's SettingsPanel::BlocksInput() returns early
	// from its input sink when it is true, and Simple Power Attack imports the same export. Two
	// users reported hotkeys dead under AMF but fine on SKSE Menu Framework.
	//
	// The aggregate alone is not enough to act on: with several windows registered, "true" does not
	// say WHICH one is latched open, and guessing among them is what has made this defect take three
	// passes. Each entry names itself so one measurement identifies the culprit.
	struct WindowState
	{
		bool open{ false };
		bool blocking{ false };
		std::string view;   // AddWindowWithView's name; empty for a plain AddWindow
	};

	std::vector<WindowState> WindowStates();
}
