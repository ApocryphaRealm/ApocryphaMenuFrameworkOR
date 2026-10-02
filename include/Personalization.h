#pragma once

// ============================================================================================
// Menu-shell personalization (author verdict 2026-09-01, queued with the controller-nav work):
// the player may RENAME a mod's menu entry and REORDER the list. Deliberately NOT included:
// repositioning or floating per-mod windows ("no repositioning").
//
// This is a PRESENTATION layer over registry::Snapshot() - registered mods are untouched and
// know nothing about it. Two pieces of state, both stored in AMF's own INI:
//
//   ALIAS   a player-facing name per mod. Where an alias is set it replaces the mod's own name
//           in the list, in the content pane's heading, and in alphabetical sorting.
//
//   FAVOURITE  a menu the player has pinned (the owner, 2026-09-19, from phbd01's report: "there
//           doesn't seem to be an option to automatically pin a menu as a favorite/first item").
//           Favourites float to the TOP of the list in the order they were favourited, so a new
//           favourite lands after the last one, and every entry's position number re-flows around
//           them exactly as a typed position does. A favourited entry is marked in the list with a
//           filled white box to the left of its name.
//
//   SORT    what the NON-favourite remainder is ordered by: the list's own order (the custom
//           sequence when one is set, else alphabetical), forced A-Z, or forced Z-A. The two
//           sidebar toggles under "Mods" set it (the owner, 2026-09-19).
//
//   ORDER   the author's option B: the default is alphabetical, EVERY entry always shows its
//           position number, and typing a new number MOVES that entry there while every other
//           number re-flows (insert-and-shift, playlist-style). His reason for rejecting
//           "number only the ones you care about": it would force players to number the lot by
//           hand. A mod installed later inserts at its alphabetical position within the
//           existing sequence rather than landing at the end.
//
//   SEPARATOR  (the owner, 2026-10-02, MO2's separators: "press Y on controller or right click on mouse to create a
//           separator and the separator should function just like a mod in the rename and reorder function ... mods
//           menus are children of the separator above them like in mo2", then "the mods can be collapsed into the
//           separator" and "a send to option for sending the selected mod to a separator"). A separator is an ENTRY
//           in the custom sequence named kSeparatorPrefix + a number, so the machinery a mod row has works on it as it
//           is: its name is its alias, favouriting pins it, typing a position moves it. The mods after a separator,
//           up to the next one, are its children; a collapsed separator hides them in the side list. Decided the same
//           day: A-Z / Z-A sort only the mods NOT under a separator ("let's make the A to Z sorting ignore mods in a
//           separator"); a separator can be favourited ("you can also favorite separators") and pins with its mods;
//           favourite MODS stay at the very top ("favorite mods stay at the top").
// ============================================================================================

#include "Registry.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace personalization
{
	struct DisplayEntry
	{
		int registryIndex = 0;      // index into the registry snapshot this row draws
		std::string modName;        // the mod's own registered name (identity - never shown when aliased)
		std::string displayName;    // alias when set, else modName (what the list shows and sorts by)
		bool separator = false;     // a separator row (registryIndex is -1)
		int depth = 0;              // 1 = a mod under a separator
		bool hidden = false;        // a mod under a COLLAPSED separator (the side list skips it, the menu-list page shows it)
		bool collapsed = false;     // a separator whose mods are folded away
		int children = 0;           // a separator's mod count
	};

	inline constexpr const char* kSeparatorPrefix = "::sep:";
	bool IsSeparator(const std::string& a_name);

	// The list the menu draws, in display order, one row per registered mod.
	std::vector<DisplayEntry> Order(const std::vector<registry::Entry>& a_entries);

	// Alias: empty string clears it (the entry falls back to the mod's own name).
	std::string GetAlias(const std::string& a_modName);
	void SetAlias(const std::string& a_modName, const std::string& a_alias);

	// Move a mod to 1-based position a_position within the current display order; every other
	// entry re-flows around it. Out-of-range positions clamp. Switches the list to custom order.
	// A SEPARATOR moves with its mods (the whole group lands at the position).
	void MoveTo(const std::vector<registry::Entry>& a_entries, const std::string& a_modName, int a_position);

	// ---- Separators ---------------------------------------------------------------------------
	// A new separator named a_name, placed directly above a_beforeName (at the end when a_beforeName is empty or not in
	// the list). Switches the list to custom order. Returns the separator's identity (kSeparatorPrefix + a number).
	std::string AddSeparator(const std::vector<registry::Entry>& a_entries, const std::string& a_beforeName, const std::string& a_name);
	// Removes it; its mods join the separator above it (or the top of the list). Its name, pin and fold go with it.
	bool RemoveSeparator(const std::string& a_separator);
	// Sends a mod to the end of a separator's group; an empty a_separator sends it out of every group (the end of the
	// mods above the first separator).
	bool SendTo(const std::vector<registry::Entry>& a_entries, const std::string& a_modName, const std::string& a_separator);
	// "Move to the top" for a MOD (the owner, 2026-10-02: "the move to top button moves it to the top of the separator that
	// it's in. That way it's distinct from favoriting"): the first place in its own group - right under its separator, or
	// the head of the loose mods when it is in none.
	bool MoveToGroupTop(const std::vector<registry::Entry>& a_entries, const std::string& a_modName);
	// "Reorder" (the owner, 2026-10-02: "two little arrows on it which moves the mod up or down by one position"): one step
	// up (a_direction -1) or down (+1) past the next VISIBLE row - a folded group's hidden mods are stepped over, and
	// stepping past a separator carries the mod into or out of that group, as in MO2. false at either end.
	bool Nudge(const std::vector<registry::Entry>& a_entries, const std::string& a_modName, int a_direction);
	struct SeparatorInfo
	{
		std::string id;
		std::string name;
	};
	std::vector<SeparatorInfo> Separators();   // in list order
	bool IsCollapsed(const std::string& a_separator);
	void ToggleCollapsed(const std::string& a_separator);

	// Custom order on/off. Off = pure alphabetical by display name (the default).
	bool IsCustomOrder();
	void ResetToAlphabetical();

	// ---- Favourites (pinned menus) ---------------------------------------------------------
	// A favourite is pinned to the top of the list. The pin block keeps the order the player
	// favourited them in, so "subsequent favorites go after the last favorited item" is simply
	// where a newly favourited name is appended.
	bool IsFavourite(const std::string& a_modName);
	void SetFavourite(const std::string& a_modName, bool a_favourite);
	void ToggleFavourite(const std::string& a_modName);
	// 1-based position in the pin block, 0 when the mod is not pinned - what the list's number
	// column shows for it.
	int FavouritePosition(const std::string& a_modName);
	std::size_t FavouriteCount();

	// ---- Sort of the non-favourite remainder ------------------------------------------------
	enum class SortMode : int
	{
		kListOrder = 0,   // the custom sequence when one is set, else alphabetical (the default)
		kAlphaAsc = 1,    // forced A-Z, whatever the custom sequence says
		kAlphaDesc = 2    // forced Z-A
	};
	SortMode GetSortMode();
	void SetSortMode(SortMode a_mode);

	// INI plumbing, called by settings::Load/Save so all of AMF's state lives in one file.
	void LoadFrom(const std::unordered_map<std::string, std::string>& a_iniEntries);
	std::string IniBlock();
}
