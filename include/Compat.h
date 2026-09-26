#pragma once

// ============================================================================================
// M3 second half: the SMF-compatible export surface, sized EXACTLY from the project's own
// export inventory (3. analyze mods\AMF export inventory\inventory.md - 39 names our 11 mods
// actually resolve, plus the optional unregister/version names their vendored header probes).
//
// ORIGINALITY RAIL: everything here is derived from OUR repos' consumer-side vendored header
// and the public cimgui naming conventions (MIT). SKSE Menu Framework's source and binary were
// never read. Signatures come from the inventory's master table verbatim.
// ============================================================================================

namespace RE
{
	class InputEvent;
}

namespace compat
{
	// Menu lifecycle events for the RegisterEventPriority consumers (Wait Your Turn Redux).
	// Values mirror the consumer-side header's own enum: 1 open, 2 close, 3/4 around render.
	enum class MenuEvent : int
	{
		kOpenMenu = 1,
		kCloseMenu = 2,
		kBeforeRender = 3,
		kAfterRender = 4,
	};

	// Render thread. Fires every registered event callback in priority order.
	void FireMenuEvent(MenuEvent a_event);

	// Input thread, called from the poll hook for EVERY event - menu open or closed. Returns true
	// if any registered input callback consumed it.
	//
	// The caller's handling differs by menu state, and the difference is the point:
	//   * menu OPEN   - the event is consumed either way (the game sees nothing while the menu is
	//                   up); a true return only means ImGui is not fed it as well.
	//   * menu CLOSED - the event passes through to the game UNLESS a callback returns true. This
	//                   is what makes a consumer's hotkey work during normal play.
	//
	// This was previously called only in the menu-open branch, and this comment said so as though
	// it were the design. It was not: it is why every hotkey registered through
	// RegisterInpoutEvent/AddInputEvent was dead outside the menu (Simple Power Attack and
	// SkyPlace, reported 2026-09-11; real SMF dispatches these regardless of its own menu state).
	bool DispatchInputEvent(RE::InputEvent* a_event);

	// ----------------------------------------------------------------------------------------
	// LAUNCHER CONTROL (1.4.10). A menu-launcher mod - one that gathers other mods' menus behind
	// a single key - drives a menu framework through three exports rather than an API header:
	// it takes GetMainWindow's object and writes IsOpen on it, asks IsAnyBlockingWindowOpened
	// whether a menu is already up, and calls SetHotkeyEnabled to silence the framework's own
	// key while it manages the binding. Those three are exported from Compat.cpp; the two below
	// are the inside half that makes them mean something.
	//
	// Found by testing against Risa's All In One Menu 4.9 (2026-09-03): it already finds AMF,
	// because AMF answers to the SKSEMenuFramework.dll alias, and then its button did nothing -
	// GetMainWindow was not exported, so it got null and logged an error. Nothing about the
	// launcher needed to change; the missing half was ours.
	// ----------------------------------------------------------------------------------------

	// Render thread, once per frame. Reconciles the exported window object with the framework's
	// own visibility in BOTH directions: an outside writer's change opens or closes the menu,
	// and a change made in game (the toggle key, a DevBench call) is published back so the
	// launcher's idea of "is it open" stays true.
	void PumpExternalWindow();

	// False while a launcher has taken the key over via SetHotkeyEnabled(false). Runtime only -
	// it is never written to the INI, so the player's own binding survives the launcher being
	// uninstalled.
	bool IsHotkeyEnabled();
}
