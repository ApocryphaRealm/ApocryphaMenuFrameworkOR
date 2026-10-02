#include "Personalization.h"

#include "Logger.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <cstring>
#include <format>
#include <sstream>
#include <unordered_set>

namespace personalization
{
	namespace
	{
		std::mutex g_lock;

		// Mod name -> player-facing alias. Only non-empty aliases are kept.
		std::unordered_map<std::string, std::string> g_alias;

		// The custom sequence, by MOD NAME (identity survives a rename). Names of mods that are
		// not currently registered stay in the list so uninstalling and reinstalling a mod does
		// not lose its place.
		std::vector<std::string> g_order;
		bool g_customOrder = false;

		// Pinned menus, in the order the player favourited them. Like g_order this is keyed by MOD
		// NAME, so a rename keeps the pin and uninstalling a mod does not lose its place.
		std::vector<std::string> g_favourites;

		// Separators whose mods are folded away in the side list.
		std::unordered_set<std::string> g_collapsed;

		// What the non-favourite remainder is sorted by (the sidebar's two toggles).
		SortMode g_sortMode = SortMode::kListOrder;

		std::string Lower(std::string a_text)
		{
			for (char& c : a_text) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_text;
		}

		std::string DisplayNameLocked(const std::string& a_modName)
		{
			const auto it = g_alias.find(a_modName);
			if (it != g_alias.end() && !it->second.empty()) { return it->second; }
			// a separator always carries a name (the page names it on creation); this is only the fallback for a hand-edited INI
			return IsSeparator(a_modName) ? std::string("Separator") : a_modName;
		}

		// Alphabetical by display name, case-insensitive, ties broken by the mod's own name so
		// the order is total and stable.
		bool AlphaLess(const std::string& a_lhs, const std::string& a_rhs)
		{
			const std::string l = Lower(DisplayNameLocked(a_lhs));
			const std::string r = Lower(DisplayNameLocked(a_rhs));
			return l != r ? l < r : a_lhs < a_rhs;
		}

		bool Contains(const std::vector<std::string>& a_list, const std::string& a_name)
		{
			return std::find(a_list.begin(), a_list.end(), a_name) != a_list.end();
		}

		// THE CANONICAL SEQUENCE - what g_order stores and every edit rewrites. Positional, like MO2's mod list: the mods
		// before the first separator are loose; every other mod belongs to the nearest separator above it. In custom mode
		// the stored sequence leads and a mod new to it is inserted at its alphabetical position among the LOOSE mods (the
		// author: a later install must not just land at the end; and it must not fall into whatever group sits there).
		// Off custom mode it is plain alphabetical (no separators exist then - creating one turns custom mode on). A forced
		// A-Z / Z-A sorts the loose mods only (the owner, 2026-10-02: "let's make the A to Z sorting ignore mods in a
		// separator"); it is a way of LOOKING at the list, so g_order is not rewritten by it - only an edit bakes it in.
		std::vector<std::string> CanonicalLocked(const std::vector<registry::Entry>& a_entries)
		{
			std::vector<std::string> present;
			present.reserve(a_entries.size());
			for (const registry::Entry& entry : a_entries) { present.push_back(entry.modName); }

			std::vector<std::string> sequence;
			if (!g_customOrder)
			{
				sequence = present;
				std::sort(sequence.begin(), sequence.end(), AlphaLess);
			}
			else
			{
				sequence.reserve(g_order.size() + present.size());
				for (const std::string& name : g_order)
				{
					if ((IsSeparator(name) || Contains(present, name)) && !Contains(sequence, name)) { sequence.push_back(name); }
				}
				std::vector<std::string> newcomers;
				for (const std::string& name : present)
				{
					if (!Contains(sequence, name)) { newcomers.push_back(name); }
				}
				std::sort(newcomers.begin(), newcomers.end(), AlphaLess);
				for (const std::string& name : newcomers)
				{
					const auto firstSeparator = std::find_if(sequence.begin(), sequence.end(), [](const std::string& n) { return IsSeparator(n); });
					const auto at = std::find_if(sequence.begin(), firstSeparator,
						[&](const std::string& existing) { return AlphaLess(name, existing); });
					sequence.insert(at, name);
				}
			}
			if (g_sortMode != SortMode::kListOrder)
			{
				const auto firstSeparator = std::find_if(sequence.begin(), sequence.end(), [](const std::string& n) { return IsSeparator(n); });
				std::sort(sequence.begin(), firstSeparator, AlphaLess);
				if (g_sortMode == SortMode::kAlphaDesc) { std::reverse(sequence.begin(), firstSeparator); }
			}
			return sequence;
		}

		struct Group
		{
			std::string separator;              // empty = the loose mods
			std::vector<std::string> mods;
		};

		std::vector<Group> GroupsOf(const std::vector<std::string>& a_canonical)
		{
			std::vector<Group> groups(1);   // [0] = the loose mods, always present
			for (const std::string& name : a_canonical)
			{
				if (IsSeparator(name)) { groups.push_back({ name, {} }); }
				else { groups.back().mods.push_back(name); }
			}
			return groups;
		}

		// THE DISPLAY - what the side list draws, top to bottom: favourite MODS first in the order they were favourited
		// (pulled out of whatever group holds them - "favorite mods stay at the top"), then favourite SEPARATORS with
		// their mods, in favourite order ("you can also favorite separators"), then the loose mods, then every other
		// separator with its mods, in list order.
		std::vector<DisplayEntry> DisplayLocked(const std::vector<registry::Entry>& a_entries)
		{
			std::vector<Group> groups = GroupsOf(CanonicalLocked(a_entries));

			std::vector<std::string> favouriteMods;
			for (const std::string& name : g_favourites)
			{
				if (IsSeparator(name)) { continue; }
				for (Group& group : groups)
				{
					const auto at = std::find(group.mods.begin(), group.mods.end(), name);
					if (at != group.mods.end())
					{
						favouriteMods.push_back(name);
						group.mods.erase(at);
						break;
					}
				}
			}
			std::vector<Group> favouriteGroups;
			for (const std::string& name : g_favourites)
			{
				if (!IsSeparator(name)) { continue; }
				const auto at = std::find_if(groups.begin() + 1, groups.end(), [&](const Group& g) { return g.separator == name; });
				if (at != groups.end())
				{
					favouriteGroups.push_back(std::move(*at));
					groups.erase(at);
				}
			}

			const auto indexOf = [&](const std::string& a_name) {
				for (int i = 0; i < static_cast<int>(a_entries.size()); ++i)
				{
					if (a_entries[i].modName == a_name) { return i; }
				}
				return -1;
			};
			std::vector<DisplayEntry> rows;
			rows.reserve(a_entries.size() + groups.size() + favouriteGroups.size());
			const auto addMod = [&](const std::string& a_name, int a_depth, bool a_hidden) {
				DisplayEntry row;
				row.registryIndex = indexOf(a_name);
				row.modName = a_name;
				row.displayName = DisplayNameLocked(a_name);
				row.depth = a_depth;
				row.hidden = a_hidden;
				rows.push_back(std::move(row));
			};
			const auto addGroup = [&](const Group& a_group) {
				DisplayEntry row;
				row.registryIndex = -1;
				row.modName = a_group.separator;
				row.displayName = DisplayNameLocked(a_group.separator);
				row.separator = true;
				row.collapsed = g_collapsed.contains(a_group.separator);
				row.children = static_cast<int>(a_group.mods.size());
				rows.push_back(std::move(row));
				for (const std::string& name : a_group.mods) { addMod(name, 1, g_collapsed.contains(a_group.separator)); }
			};
			for (const std::string& name : favouriteMods) { addMod(name, 0, false); }
			for (const Group& group : favouriteGroups) { addGroup(group); }
			for (const std::string& name : groups[0].mods) { addMod(name, 0, false); }
			for (std::size_t g = 1; g < groups.size(); ++g) { addGroup(groups[g]); }
			return rows;
		}

		// The separator's block in the canonical sequence: [first, last) - the separator and every mod under it.
		std::pair<std::size_t, std::size_t> BlockOf(const std::vector<std::string>& a_canonical, const std::string& a_separator)
		{
			const auto at = std::find(a_canonical.begin(), a_canonical.end(), a_separator);
			if (at == a_canonical.end()) { return { a_canonical.size(), a_canonical.size() }; }
			const std::size_t first = static_cast<std::size_t>(std::distance(a_canonical.begin(), at));
			std::size_t last = first + 1;
			while (last < a_canonical.size() && !IsSeparator(a_canonical[last])) { ++last; }
			return { first, last };
		}

		int NextSeparatorNumberLocked()
		{
			int highest = 0;
			const auto scan = [&](const std::string& a_name) {
				if (IsSeparator(a_name)) { highest = std::max(highest, std::atoi(a_name.c_str() + std::strlen(kSeparatorPrefix))); }
			};
			for (const std::string& name : g_order) { scan(name); }
			for (const auto& [name, alias] : g_alias) { scan(name); }
			return highest + 1;
		}
	}

	bool IsSeparator(const std::string& a_name)
	{
		return a_name.rfind(kSeparatorPrefix, 0) == 0;
	}

	std::vector<DisplayEntry> Order(const std::vector<registry::Entry>& a_entries)
	{
		std::scoped_lock lock(g_lock);
		return DisplayLocked(a_entries);
	}

	std::string GetAlias(const std::string& a_modName)
	{
		std::scoped_lock lock(g_lock);
		const auto it = g_alias.find(a_modName);
		return it != g_alias.end() ? it->second : std::string{};
	}

	void SetAlias(const std::string& a_modName, const std::string& a_alias)
	{
		std::scoped_lock lock(g_lock);
		if (a_alias.empty())
		{
			if (g_alias.erase(a_modName) > 0) { logger::info("menu alias cleared for \"{}\"", a_modName); }
			return;
		}
		g_alias[a_modName] = a_alias;
		logger::info("menu alias: \"{}\" shows as \"{}\"", a_modName, a_alias);
	}

	void MoveTo(const std::vector<registry::Entry>& a_entries, const std::string& a_modName, int a_position)
	{
		std::scoped_lock lock(g_lock);
		// The position is the one the player SEES (the display); the edit is made in the canonical sequence. The entry is
		// put next to whatever the display shows at that position - above it when moving up, below it when moving down -
		// so it takes that neighbour's group, as a drop does in MO2. A separator moves with its mods.
		const std::vector<DisplayEntry> display = DisplayLocked(a_entries);
		const auto fromIt = std::find_if(display.begin(), display.end(), [&](const DisplayEntry& r) { return r.modName == a_modName; });
		if (fromIt == display.end()) { return; }
		const int count = static_cast<int>(display.size());
		const int target = std::clamp(a_position, 1, count) - 1;
		const int from = static_cast<int>(std::distance(display.begin(), fromIt));
		if (from == target) { return; }
		const std::string anchor = display[static_cast<std::size_t>(target)].modName;

		std::vector<std::string> canonical = CanonicalLocked(a_entries);
		std::vector<std::string> block;
		if (IsSeparator(a_modName))
		{
			const auto [first, last] = BlockOf(canonical, a_modName);
			block.assign(canonical.begin() + static_cast<std::ptrdiff_t>(first), canonical.begin() + static_cast<std::ptrdiff_t>(last));
			if (Contains(block, anchor)) { return; }   // into its own group: nothing to do
			canonical.erase(canonical.begin() + static_cast<std::ptrdiff_t>(first), canonical.begin() + static_cast<std::ptrdiff_t>(last));
		}
		else
		{
			block.push_back(a_modName);
			canonical.erase(std::find(canonical.begin(), canonical.end(), a_modName));
		}
		auto at = std::find(canonical.begin(), canonical.end(), anchor);
		if (IsSeparator(a_modName))
		{
			// A separator dropped next to a LOOSE mod or a pinned mod would otherwise swallow the loose mods below it as
			// its own (MO2's positional rule). It goes to the head of the separators instead - "Move to the top" of a
			// separator means the first group (test 2026-10-02: it stayed put under a pinned separator).
			const auto firstSeparator = std::find_if(canonical.begin(), canonical.end(), [](const std::string& n) { return IsSeparator(n); });
			const bool anchorLoose = at == canonical.end() || at < firstSeparator;
			const bool anchorPinnedMod = !IsSeparator(anchor) && Contains(g_favourites, anchor);
			if (anchorLoose || anchorPinnedMod) { at = firstSeparator; }
			else if (target > from) { ++at; }
		}
		else if (at != canonical.end() && target > from) { ++at; }   // moving down: below the anchor
		canonical.insert(at, block.begin(), block.end());

		g_order = canonical;
		g_customOrder = true;
		logger::info("menu order: \"{}\"{} moved from position {} to {} (next to \"{}\"; {} entries)", a_modName,
					 block.size() > 1 ? std::format(" with its {} mod(s)", block.size() - 1) : std::string(), from + 1, target + 1, anchor, count);
	}

	std::string AddSeparator(const std::vector<registry::Entry>& a_entries, const std::string& a_beforeName, const std::string& a_name)
	{
		std::scoped_lock lock(g_lock);
		std::vector<std::string> canonical = CanonicalLocked(a_entries);
		const std::string id = std::string(kSeparatorPrefix) + std::to_string(NextSeparatorNumberLocked());
		const auto at = std::find(canonical.begin(), canonical.end(), a_beforeName);
		canonical.insert(at, id);   // end() when a_beforeName is empty or unknown
		g_order = canonical;
		g_customOrder = true;
		if (!a_name.empty()) { g_alias[id] = a_name; }
		logger::info("menu separator {} \"{}\" added {}", id, a_name, a_beforeName.empty() || at == canonical.end() ? "at the end" : "above \"" + a_beforeName + "\"");
		return id;
	}

	bool RemoveSeparator(const std::string& a_separator)
	{
		std::scoped_lock lock(g_lock);
		if (!IsSeparator(a_separator)) { return false; }
		const auto at = std::find(g_order.begin(), g_order.end(), a_separator);
		const bool found = at != g_order.end();
		if (found) { g_order.erase(at); }   // its mods now follow the separator above it (or are loose)
		g_alias.erase(a_separator);
		g_collapsed.erase(a_separator);
		g_favourites.erase(std::remove(g_favourites.begin(), g_favourites.end(), a_separator), g_favourites.end());
		logger::info("menu separator {} removed{}", a_separator, found ? "" : " (it was not in the list)");
		return found;
	}

	bool SendTo(const std::vector<registry::Entry>& a_entries, const std::string& a_modName, const std::string& a_separator)
	{
		std::scoped_lock lock(g_lock);
		if (IsSeparator(a_modName)) { return false; }
		std::vector<std::string> canonical = CanonicalLocked(a_entries);
		const auto self = std::find(canonical.begin(), canonical.end(), a_modName);
		if (self == canonical.end()) { return false; }
		canonical.erase(self);
		std::size_t insertAt = 0;
		if (a_separator.empty())
		{
			// out of every group: the end of the loose mods
			const auto firstSeparator = std::find_if(canonical.begin(), canonical.end(), [](const std::string& n) { return IsSeparator(n); });
			insertAt = static_cast<std::size_t>(std::distance(canonical.begin(), firstSeparator));
		}
		else
		{
			const auto [first, last] = BlockOf(canonical, a_separator);
			if (first >= canonical.size()) { return false; }
			insertAt = last;   // the end of that separator's group
		}
		canonical.insert(canonical.begin() + static_cast<std::ptrdiff_t>(insertAt), a_modName);
		g_order = canonical;
		g_customOrder = true;
		logger::info("menu order: \"{}\" sent to {}", a_modName, a_separator.empty() ? std::string("the loose mods") : "separator " + a_separator);
		return true;
	}

	bool MoveToGroupTop(const std::vector<registry::Entry>& a_entries, const std::string& a_modName)
	{
		std::scoped_lock lock(g_lock);
		if (IsSeparator(a_modName)) { return false; }
		std::vector<std::string> canonical = CanonicalLocked(a_entries);
		const auto self = std::find(canonical.begin(), canonical.end(), a_modName);
		if (self == canonical.end()) { return false; }
		// its group's head: just after the nearest separator above it, or the very start (the loose mods)
		std::size_t head = 0;
		for (auto it = self; it != canonical.begin();)
		{
			--it;
			if (IsSeparator(*it)) { head = static_cast<std::size_t>(std::distance(canonical.begin(), it)) + 1; break; }
		}
		const std::size_t from = static_cast<std::size_t>(std::distance(canonical.begin(), self));
		if (from == head) { return true; }
		canonical.erase(self);
		canonical.insert(canonical.begin() + static_cast<std::ptrdiff_t>(head), a_modName);
		g_order = canonical;
		g_customOrder = true;
		logger::info("menu order: \"{}\" moved to the top of {}", a_modName, head == 0 ? std::string("the loose mods") : "separator " + canonical[head - 1]);
		return true;
	}

	bool Nudge(const std::vector<registry::Entry>& a_entries, const std::string& a_modName, int a_direction)
	{
		int target = 0;
		{
			std::scoped_lock lock(g_lock);
			const std::vector<DisplayEntry> display = DisplayLocked(a_entries);
			std::vector<int> visible;   // indices into display of the rows the side list shows
			for (int i = 0; i < static_cast<int>(display.size()); ++i)
			{
				if (!display[static_cast<std::size_t>(i)].hidden) { visible.push_back(i); }
			}
			int at = -1;
			for (int v = 0; v < static_cast<int>(visible.size()); ++v)
			{
				if (display[static_cast<std::size_t>(visible[static_cast<std::size_t>(v)])].modName == a_modName) { at = v; break; }
			}
			const int to = at + (a_direction < 0 ? -1 : 1);
			if (at < 0 || to < 0 || to >= static_cast<int>(visible.size())) { return false; }
			target = visible[static_cast<std::size_t>(to)] + 1;   // MoveTo takes the 1-based display position
		}
		MoveTo(a_entries, a_modName, target);
		return true;
	}

	std::vector<SeparatorInfo> Separators()
	{
		std::scoped_lock lock(g_lock);
		std::vector<SeparatorInfo> out;
		for (const std::string& name : g_order)
		{
			if (IsSeparator(name)) { out.push_back({ name, DisplayNameLocked(name) }); }
		}
		return out;
	}

	bool IsCollapsed(const std::string& a_separator)
	{
		std::scoped_lock lock(g_lock);
		return g_collapsed.contains(a_separator);
	}

	void ToggleCollapsed(const std::string& a_separator)
	{
		std::scoped_lock lock(g_lock);
		if (!g_collapsed.erase(a_separator)) { g_collapsed.insert(a_separator); }
		logger::info("menu separator {} {}", a_separator, g_collapsed.contains(a_separator) ? "collapsed" : "expanded");
	}

	bool IsFavourite(const std::string& a_modName)
	{
		std::scoped_lock lock(g_lock);
		return std::find(g_favourites.begin(), g_favourites.end(), a_modName) != g_favourites.end();
	}

	void SetFavourite(const std::string& a_modName, bool a_favourite)
	{
		std::scoped_lock lock(g_lock);
		const auto at = std::find(g_favourites.begin(), g_favourites.end(), a_modName);
		if (a_favourite)
		{
			// Appended, never inserted: the pin block IS the favourite order, so a new favourite
			// takes the position after the last one (the owner, 2026-09-19).
			if (at == g_favourites.end())
			{
				g_favourites.push_back(a_modName);
				logger::info("menu favourite: \"{}\" pinned at position {}", a_modName, g_favourites.size());
			}
			return;
		}
		if (at != g_favourites.end())
		{
			g_favourites.erase(at);
			logger::info("menu favourite: \"{}\" unpinned ({} left)", a_modName, g_favourites.size());
		}
	}

	void ToggleFavourite(const std::string& a_modName)
	{
		bool on = false;
		{
			std::scoped_lock lock(g_lock);
			on = std::find(g_favourites.begin(), g_favourites.end(), a_modName) != g_favourites.end();
		}
		SetFavourite(a_modName, !on);
	}

	int FavouritePosition(const std::string& a_modName)
	{
		std::scoped_lock lock(g_lock);
		const auto at = std::find(g_favourites.begin(), g_favourites.end(), a_modName);
		return at == g_favourites.end() ? 0 : static_cast<int>(std::distance(g_favourites.begin(), at)) + 1;
	}

	std::size_t FavouriteCount()
	{
		std::scoped_lock lock(g_lock);
		return g_favourites.size();
	}

	SortMode GetSortMode()
	{
		std::scoped_lock lock(g_lock);
		return g_sortMode;
	}

	void SetSortMode(SortMode a_mode)
	{
		std::scoped_lock lock(g_lock);
		if (g_sortMode == a_mode) { return; }
		g_sortMode = a_mode;
		logger::info("menu sort: {}", a_mode == SortMode::kAlphaAsc ? "A-Z" :
					 a_mode == SortMode::kAlphaDesc ? "Z-A" : "list order");
	}

	bool IsCustomOrder()
	{
		std::scoped_lock lock(g_lock);
		return g_customOrder;
	}

	void ResetToAlphabetical()
	{
		std::scoped_lock lock(g_lock);
		// The mods go back to alphabetical and out of every group; the separators the player made are kept (empty, at the
		// end, in their order) rather than deleted by a sort reset - removing one is its own action.
		std::vector<std::string> separators;
		for (const std::string& name : g_order)
		{
			if (IsSeparator(name)) { separators.push_back(name); }
		}
		g_order = separators;
		g_customOrder = !separators.empty();
		logger::info("menu order reset to alphabetical ({} separator(s) kept)", separators.size());
	}

	void LoadFrom(const std::unordered_map<std::string, std::string>& a_iniEntries)
	{
		std::scoped_lock lock(g_lock);
		g_alias.clear();
		g_order.clear();
		g_customOrder = false;
		g_favourites.clear();
		g_sortMode = SortMode::kListOrder;
		g_collapsed.clear();

		constexpr std::string_view kAliasPrefix = "MenuAlias.";
		for (const auto& [key, value] : a_iniEntries)
		{
			if (key.compare(0, kAliasPrefix.size(), kAliasPrefix) == 0 && !value.empty())
			{
				g_alias[key.substr(kAliasPrefix.size())] = value;
			}
		}

		if (const auto it = a_iniEntries.find("MenuOrder.bCustomOrder"); it != a_iniEntries.end())
		{
			g_customOrder = it->second != "0";
		}
		if (const auto it = a_iniEntries.find("MenuOrder.sOrder"); it != a_iniEntries.end() && !it->second.empty())
		{
			// Pipe-separated: mod names carry spaces and apostrophes, never pipes.
			std::stringstream stream(it->second);
			std::string name;
			while (std::getline(stream, name, '|'))
			{
				if (!name.empty()) { g_order.push_back(name); }
			}
		}
		const auto readList = [&](const char* a_key, std::vector<std::string>& a_out) {
			const auto it = a_iniEntries.find(a_key);
			if (it == a_iniEntries.end() || it->second.empty()) { return; }
			std::stringstream stream(it->second);
			std::string name;
			while (std::getline(stream, name, '|'))
			{
				if (!name.empty()) { a_out.push_back(name); }
			}
		};
		readList("MenuFavourites.sFavourites", g_favourites);
		{
			std::vector<std::string> folded;
			readList("MenuSeparators.sCollapsed", folded);
			g_collapsed.insert(folded.begin(), folded.end());
		}

		if (const auto it = a_iniEntries.find("MenuOrder.iSort"); it != a_iniEntries.end())
		{
			const int raw = std::atoi(it->second.c_str());
			g_sortMode = (raw == 1) ? SortMode::kAlphaAsc : (raw == 2) ? SortMode::kAlphaDesc : SortMode::kListOrder;
		}

		logger::info("menu personalization loaded: {} alias(es), custom order {} ({} remembered position(s)), "
					 "{} favourite(s), sort {}",
					 g_alias.size(), g_customOrder ? "on" : "off", g_order.size(), g_favourites.size(),
					 g_sortMode == SortMode::kAlphaAsc ? "A-Z" : g_sortMode == SortMode::kAlphaDesc ? "Z-A" : "list order");
	}

	std::string IniBlock()
	{
		std::scoped_lock lock(g_lock);
		std::string text =
			"\n[MenuAlias]\n"
			"; Player-facing names for mods' menu entries: <mod's registered name>=<what to show>.\n"
			"; Set from the Framework Settings page; the mod itself is untouched.\n";
		for (const auto& [modName, alias] : g_alias)
		{
			if (!alias.empty()) { text += modName + "=" + alias + "\n"; }
		}

		text +=
			"\n[MenuOrder]\n"
			"; 0 = alphabetical (the default), 1 = the custom sequence below.\n"
			"bCustomOrder=";
		text += g_customOrder ? "1" : "0";
		text +=
			"\n; The custom sequence, pipe-separated, in list order. Typing a position number on\n"
			"; the Framework Settings page rewrites this; mods missing from it insert at their\n"
			"; alphabetical position.\n"
			"sOrder=";
		for (std::size_t i = 0; i < g_order.size(); ++i)
		{
			if (i != 0) { text += "|"; }
			text += g_order[i];
		}
		text +=
			"\n; What the list is sorted by: 0 = the order above (or alphabetical when it is off),\n"
			"; 1 = forced A-Z, 2 = forced Z-A. The two toggles under \"Mods\" in the menu set it.\n"
			"iSort=";
		text += std::to_string(static_cast<int>(g_sortMode));

		text +=
			"\n\n[MenuFavourites]\n"
			"; Pinned menus, pipe-separated, in the order they were favourited. They sit at the top\n"
			"; of the list whatever the sort is, and each shows a filled white box beside its name.\n"
			"; Right-click a menu (or press Y on a controller) to pin or unpin it.\n"
			"sFavourites=";
		for (std::size_t i = 0; i < g_favourites.size(); ++i)
		{
			if (i != 0) { text += "|"; }
			text += g_favourites[i];
		}
		text +=
			"\n\n[MenuSeparators]\n"
			"; Separators are entries in sOrder named ::sep:<number>; their names are in [MenuAlias]. The mods after a\n"
			"; separator, up to the next one, are its mods. Separators whose mods are folded away in the list:\n"
			"sCollapsed=";
		{
			bool first = true;
			for (const std::string& name : g_order)
			{
				if (IsSeparator(name) && g_collapsed.contains(name))
				{
					if (!first) { text += "|"; }
					text += name;
					first = false;
				}
			}
		}
		text += "\n";
		return text;
	}
}
