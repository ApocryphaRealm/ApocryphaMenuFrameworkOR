#include "Persistence.h"

#include "Logger.h"

#include <ShlObj.h>

#include <fstream>
#include <mutex>
#include <unordered_map>

namespace persistence
{
	namespace
	{
		std::mutex g_lock;
		std::unordered_map<std::string, std::string> g_values;
		std::string g_activeSaveBaseName;  // set by OnLoadGame/OnSaveGame; empty until the first one fires
		std::string g_pendingLoadName;      // set at kPreLoadGame, consumed at kPostLoadGame

		// The game's save directory = Documents\My Games\Oblivion Remastered\Saved\SaveGames\ (Unreal's own save
		// folder; there is no sLocalSavePath to read as there was on Skyrim).
		std::string SavesDirectory()
		{
			PWSTR path = nullptr;
			std::string result;

			if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &path)) && path)
			{
				char narrow[MAX_PATH]{};
				const int written = WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof(narrow), nullptr, nullptr);
				if (written > 0)
				{
					result = narrow;
					result += "\\My Games\\Oblivion Remastered\\Saved\\SaveGames\\";
				}
			}

			if (path)
			{
				CoTaskMemFree(path);
			}

			return result;
		}

		// Strip a trailing ".ess" (SKSE's save-name payload has historically included it in some
		// versions, not others) so the sibling file's base name always matches regardless.
		std::string StripExtension(std::string_view a_name)
		{
			std::string name(a_name);
			if (name.size() > 4 && name.substr(name.size() - 4) == ".ess")
			{
				name.resize(name.size() - 4);
			}
			return name;
		}

		std::string SiblingPath(const std::string& a_baseName)
		{
			return SavesDirectory() + a_baseName + ".amf-state.ini";
		}
	}

	void OnSaveGame(std::string_view a_saveName)
	{
		const std::string baseName = StripExtension(a_saveName);
		if (baseName.empty())
		{
			logger::warn("persistence: OnSaveGame received an empty save name; nothing written");
			return;
		}

		std::unordered_map<std::string, std::string> snapshot;
		{
			std::scoped_lock lock(g_lock);
			g_activeSaveBaseName = baseName;
			snapshot = g_values;
		}

		const std::string path = SiblingPath(baseName);
		std::ofstream file(path, std::ios::trunc);
		if (!file.is_open())
		{
			logger::error("persistence: could not open {} for writing; state for this save was NOT committed", path);
			return;
		}

		file << "; AMF per-save state - mirrors co-save's scoping (decisions doc S10), plain text.\n"
				"; Written on save, read back on load of THIS specific save. Safe to inspect,\n"
				"; unsafe to hand-edit while the game has this save loaded.\n\n";

		for (const auto& [key, value] : snapshot)
		{
			file << key << "=" << value << "\n";
		}

		logger::info("persistence: {} value(s) committed to {}", snapshot.size(), path);
	}

	void OnPreLoadGame(std::string_view a_saveName)
	{
		// kPreLoadGame's payload is the save name (char* + dataLen) - stash it for post-load.
		std::string baseName = StripExtension(a_saveName);
		std::scoped_lock lock(g_lock);
		g_pendingLoadName = std::move(baseName);
	}

	void OnPostLoadGame(bool a_loadSucceeded)
	{
		std::string name;
		{
			std::scoped_lock lock(g_lock);
			name = g_pendingLoadName;
			g_pendingLoadName.clear();
		}

		if (!a_loadSucceeded)
		{
			logger::info("persistence: load reported unsuccessful; state left at defaults");
			std::scoped_lock lock(g_lock);
			g_values.clear();
			g_activeSaveBaseName.clear();
			return;
		}

		if (name.empty())
		{
			// New game, or a load with no preceding kPreLoadGame name - start clean.
			logger::info("persistence: post-load with no captured save name (new game?) - starting empty");
			std::scoped_lock lock(g_lock);
			g_values.clear();
			g_activeSaveBaseName.clear();
			return;
		}

		OnLoadGame(name);
	}

	void OnLoadGame(std::string_view a_saveName)
	{
		const std::string baseName = StripExtension(a_saveName);
		if (baseName.empty())
		{
			logger::warn("persistence: OnLoadGame received an empty save name; state cleared to defaults");
			std::scoped_lock lock(g_lock);
			g_values.clear();
			g_activeSaveBaseName.clear();
			return;
		}

		const std::string path = SiblingPath(baseName);
		std::ifstream file(path);

		std::unordered_map<std::string, std::string> loaded;

		if (!file.is_open())
		{
			logger::info("persistence: no sibling state file for \"{}\" ({}) - this save has none yet, starting empty", baseName, path);
		}
		else
		{
			std::string line;
			while (std::getline(file, line))
			{
				if (line.empty() || line.front() == ';')
				{
					continue;
				}
				const auto equals = line.find('=');
				if (equals == std::string::npos)
				{
					continue;
				}
				loaded[line.substr(0, equals)] = line.substr(equals + 1);
			}
			logger::info("persistence: {} value(s) restored for save \"{}\" from {}", loaded.size(), baseName, path);
		}

		std::scoped_lock lock(g_lock);
		g_values = std::move(loaded);
		g_activeSaveBaseName = baseName;
	}

	void SetValue(const std::string& a_key, const std::string& a_value)
	{
		std::scoped_lock lock(g_lock);
		g_values[a_key] = a_value;
		logger::debug("persistence: SetValue(\"{}\", \"{}\") - staged, committed on next save", a_key, a_value);
	}

	std::string GetValue(const std::string& a_key, const std::string& a_default)
	{
		std::scoped_lock lock(g_lock);
		const auto it = g_values.find(a_key);
		return it != g_values.end() ? it->second : a_default;
	}
}
