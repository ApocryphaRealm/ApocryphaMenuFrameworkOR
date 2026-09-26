#pragma once

#include <string>

// ================================================================================================
// THE SYSTEM-MENU ROW - mod menus reached from the GAME's own pause menu.
//
// The framework's settings surface is opened from a row in the journal's System tab, next to SAVE,
// LOAD, SETTINGS and QUIT, rather than from a hotkey the player has to know about. That is where a
// player already goes looking for configuration.
//
// WHY THIS IS DONE AT RUNTIME AND NOT BY SHIPPING AN EDITED SWF
// -------------------------------------------------------------
// Design decision, 2026-09-04: "AMF should be compatible with other art replacers". The journal's
// artwork is `Interface/quest_journal.swf`, and every UI replacement mod ships its own copy of that
// exact file - Untarnished UI, Dear Diary Dark Mode, and our own Apocrypha UI Overhaul among them.
// A framework that shipped its own edited copy would collide with all of them, and whichever lost
// the load order would silently lose either its artwork or its row.
//
// So nothing is shipped. The row is pushed into the live menu when it opens, which works over
// whatever artwork happens to be installed, and needs no patch per replacer.
//
// THE TWO HALVES, AND WHY THE SECOND ONE IS THE AWKWARD ONE
// ---------------------------------------------------------
// Adding the row is a push onto the category list's `entryList` followed by `InvalidateData()`.
//
// Making it DO something is not, because `onCategoryListItemPress` is a compiled switch inside the
// SWF: a row added at runtime falls straight through to its `default:` branch, so it draws, plays
// the cancel sound, and does nothing at all.
//
// WRAPPING that handler was tried twice and does not work: `GetVariable` does not return an AS2
// function, neither off the page instance nor off `_global.SystemPage.prototype`, so there is
// nothing to read out and stash. What works instead is ADDING A SECOND LISTENER beside the menu's
// own - `addEventListener("itemPress", ...)` - and ignoring every index but our row's. That turned
// out to be the better shape anyway: the game's handler is never touched, so save, load, settings,
// controls and quit cannot be broken by anything here. Worst case our row does nothing.
//
// Everything here is best-effort and loudly logged. A menu whose structure we do not recognise
// (a replacer that rebuilt the hierarchy, or a future game patch) gets no row and a warning - it
// never gets a half-installed handler, because the wrap is only installed once the list is found.
// ================================================================================================

namespace systemrow
{
	// Registers the menu sink. Safe to call more than once; returns false only when the UI
	// singleton is not ready yet, so the caller can retry at a later SKSE message.
	bool Install();

	// True once the row has been added to a live journal at least once - the DevBench tool
	// reports this so the feature can be checked without a person watching the screen.
	bool WasInjected();

	// The path the row was found under, or "" if it has not been found yet. Logged and reported
	// rather than assumed, because it is the thing that differs between art replacers.
	const char* FoundPath();

	// 1.8.4: what the journal's category list actually holds right now - every row's text in order,
	// whether our press listener is attached, and the index our row was added at. The DevBench
	// 'systemrow' op reports it, so a report of the shape "the row sits in the wrong place and does
	// nothing when pressed" (borokoshow, against Dragonborn UI) is answered by reading the live list
	// rather than by reasoning about someone else's SWF. Empty rows when the journal is not open.
	std::string ListJson();

	// The journal PANEL's rectangle, as fractions of the screen (0..1), read off the live movie.
	//
	// Used to size the framework's window to the journal it is hosted in, so the nested surface
	// reads as a page of that menu instead of a larger window sitting on top of it (author,
	// 2026-09-04: "make the nested version fit into the journals shape so it doesn't look bigger
	// than the main menu in the nested version only").
	//
	// Measured rather than assumed, and measured EVERY time, because the number is different for
	// every art replacer - the whole reason the row is injected instead of shipped. Returns false
	// when the journal is not open or its panel cannot be measured; the caller then keeps whatever
	// it had rather than snapping to a default mid-frame.
	bool GetPanelRect(float& a_x, float& a_y, float& a_w, float& a_h);

	// 1.8.0: the journal art's pane divider (Quest Journal Overhaul's redesign), as a screen
	// fraction, once GetPanelRect has seen it; -1 when the loaded art has none. A stored nested
	// profile that starts left of it was dragged under different art and is stale.
	float PaneLeft();

	// 1.8.1: which journal art GetPanelRect last measured - "qjo-redesign" when Quest Journal
	// Overhaul's redesigned pane was found, "panel" for the game's own layout and every other
	// replacer. The nested window profile is remembered per art key.
	const char* ArtKey();

	// Any clip in the open journal, as fractions of the screen - the DevBench 'bounds' op, so a
	// new art replacer is measured rather than guessed at.
	bool MeasurePath(const std::string& a_path, float& a_x, float& a_y, float& a_w, float& a_h);
}
