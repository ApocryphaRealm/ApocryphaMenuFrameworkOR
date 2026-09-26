#include "Personalization.h"

#include "Logger.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <sstream>

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
			return (it != g_alias.end() && !it->second.empty()) ? it->second : a_modName;
		}

		// Alphabetical by display name, case-insensitive, ties broken by the mod's own name so
		// the order is total and stable.
		bool AlphaLess(const std::string& a_lhs, const std::string& a_rhs)
		{
			const std::string l = Lower(DisplayNameLocked(a_lhs));
			const std::string r = Lower(DisplayNameLocked(a_rhs));
			return l != r ? l < r : a_lhs < a_rhs;
		}

		// The display sequence of the CURRENTLY REGISTERED mods. In custom mode the stored
		// sequence leads and anything new is inserted at its alphabetical position among the
		// entries already there (the author: a later install must not just land at the end).
		// Pinned names first, in FAVOURITE order, then everything else in the order it was already
		// in. Applied last so the pin block survives every sort mode and the custom sequence alike -
		// a favourite is a pin, not another way of typing a position number.
		void HoistFavouritesLocked(std::vector<std::string>& a_sequence)
		{
			if (g_favourites.empty()) { return; }
			std::vector<std::string> pinned;
			pinned.reserve(g_favourites.size());
			for (const std::string& name : g_favourites)
			{
				const auto at = std::find(a_sequence.begin(), a_sequence.end(), name);
				if (at != a_sequence.end())
				{
					pinned.push_back(name);
					a_sequence.erase(at);
				}
			}
			a_sequence.insert(a_sequence.begin(), pinned.begin(), pinned.end());
		}

		std::vector<std::string> SequenceLocked(const std::vector<registry::Entry>& a_entries)
		{
			std::vector<std::string> present;
			present.reserve(a_entries.size());
			for (const registry::Entry& entry : a_entries) { present.push_back(entry.modName); }

			// A forced sort overrides the custom sequence outright rather than rewriting it: the
			// toggles are a way of LOOKING at the list, and turning them off must give the player
			// back the order they had arranged by hand.
			if (g_sortMode != SortMode::kListOrder)
			{
				std::sort(present.begin(), present.end(), AlphaLess);
				if (g_sortMode == SortMode::kAlphaDesc) { std::reverse(present.begin(), present.end()); }
				HoistFavouritesLocked(present);
				return present;
			}

			if (!g_customOrder)
			{
				std::sort(present.begin(), present.end(), AlphaLess);
				HoistFavouritesLocked(present);
				return present;
			}

			std::vector<std::string> sequence;
			sequence.reserve(present.size());
			for (const std::string& name : g_order)
			{
				if (std::find(present.begin(), present.end(), name) != present.end() &&
					std::find(sequence.begin(), sequence.end(), name) == sequence.end())
				{
					sequence.push_back(name);
				}
			}

			// Newcomers: alphabetical insertion point among the entries already sequenced.
			std::vector<std::string> newcomers;
			for (const std::string& name : present)
			{
				if (std::find(sequence.begin(), sequence.end(), name) == sequence.end()) { newcomers.push_back(name); }
			}
			std::sort(newcomers.begin(), newcomers.end(), AlphaLess);
			for (const std::string& name : newcomers)
			{
				const auto at = std::find_if(sequence.begin(), sequence.end(),
											 [&](const std::string& existing) { return AlphaLess(name, existing); });
				sequence.insert(at, name);
			}
			HoistFavouritesLocked(sequence);
			return sequence;
		}
	}

	std::vector<DisplayEntry> Order(const std::vector<registry::Entry>& a_entries)
	{
		std::scoped_lock lock(g_lock);
		const std::vector<std::string> sequence = SequenceLocked(a_entries);

		std::vector<DisplayEntry> rows;
		rows.reserve(sequence.size());
		for (const std::string& name : sequence)
		{
			for (int i = 0; i < static_cast<int>(a_entries.size()); ++i)
			{
				if (a_entries[i].modName == name)
				{
					rows.push_back({ i, name, DisplayNameLocked(name) });
					break;
				}
			}
		}
		return rows;
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
		std::vector<std::string> sequence = SequenceLocked(a_entries);
		const auto at = std::find(sequence.begin(), sequence.end(), a_modName);
		if (at == sequence.end()) { return; }

		const int count = static_cast<int>(sequence.size());
		const int target = std::clamp(a_position, 1, count) - 1;  // 1-based on the page, 0-based here
		const int from = static_cast<int>(std::distance(sequence.begin(), at));
		if (from == target) { return; }

		sequence.erase(at);
		sequence.insert(sequence.begin() + target, a_modName);

		// Everything below the moved entry re-flows because the sequence IS the numbering.
		g_order = sequence;
		g_customOrder = true;
		logger::info("menu order: \"{}\" moved from position {} to {} (list re-flowed, {} entries)",
					 a_modName, from + 1, target + 1, count);
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
		g_order.clear();
		g_customOrder = false;
		logger::info("menu order reset to alphabetical");
	}

	void LoadFrom(const std::unordered_map<std::string, std::string>& a_iniEntries)
	{
		std::scoped_lock lock(g_lock);
		g_alias.clear();
		g_order.clear();
		g_customOrder = false;
		g_favourites.clear();
		g_sortMode = SortMode::kListOrder;

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
		text += "\n";
		return text;
	}
}
