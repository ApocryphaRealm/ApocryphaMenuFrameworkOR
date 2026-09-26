#include "Strings.h"

#include "Settings.h"
#include "Logger.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include <windows.h>

// Declared by the renderer: flips its "rebuild the font atlas before the next frame" flag.
namespace renderer { void RequestFontRebuild(); }

namespace strings
{
	namespace
	{
		std::mutex g_lock;
		std::unordered_map<std::string, std::string> g_texts;   // key (without '$') -> UTF-8 text
		std::string g_language = "english";
		std::string g_allText;

		std::string Lower(std::string a_s)
		{
			std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}

		std::filesystem::path TranslationsDir()
		{
			// Oblivion Remastered has no Interface\Translations convention for script-extender plugins, so the
			// eleven files ship in the framework's own folder beside the DLL (OBSE\Plugins\ApocryphaMenuFramework).
			return std::filesystem::current_path() / "OBSE" / "Plugins" / "ApocryphaMenuFramework" / "Translations";
		}

		std::filesystem::path FileFor(const std::string& a_language)
		{
			return TranslationsDir() / ("ApocryphaMenuFramework_" + a_language + ".txt");
		}

		std::string GameLanguage()
		{
			// Skyrim's sLanguage:General has no Oblivion Remastered twin in the Gamebryo half (the text language is
			// Unreal's culture). Until that is read, the Windows display language picks among the eleven the
			// framework ships, and the INI's sLanguage still overrides it (Strings.h).
			wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
			if (::GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0)
			{
				const std::wstring_view n(name);
				const auto starts = [&](std::wstring_view a_p) { return n.substr(0, a_p.size()) == a_p; };
				if (starts(L"fr")) { return "french"; }
				if (starts(L"de")) { return "german"; }
				if (starts(L"it")) { return "italian"; }
				if (starts(L"es")) { return "spanish"; }
				if (starts(L"pl")) { return "polish"; }
				if (starts(L"ru")) { return "russian"; }
				if (starts(L"cs")) { return "czech"; }
				if (starts(L"ja")) { return "japanese"; }
				if (starts(L"ko")) { return "korean"; }
				if (starts(L"zh")) { return "chinese"; }
			}
			return "english";
		}

		std::string ToUtf8(const std::wstring& a_w)
		{
			if (a_w.empty()) { return {}; }
			const int n = WideCharToMultiByte(CP_UTF8, 0, a_w.data(), static_cast<int>(a_w.size()), nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<std::size_t>(n), '\0');
			WideCharToMultiByte(CP_UTF8, 0, a_w.data(), static_cast<int>(a_w.size()), out.data(), n, nullptr, nullptr);
			return out;
		}

		// The SkyUI/SKSE translation file: UTF-16LE with BOM, "$key<TAB>text" per line. Lines
		// without '$' or a tab are comments. "\n" inside a text is an actual line break.
		int ReadInto(const std::filesystem::path& a_path, std::unordered_map<std::string, std::string>& a_out, bool a_overwrite)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in) { return -1; }
			std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			if (bytes.size() < 2 || static_cast<unsigned char>(bytes[0]) != 0xFF || static_cast<unsigned char>(bytes[1]) != 0xFE) { return -2; }
			std::wstring text(reinterpret_cast<const wchar_t*>(bytes.data() + 2), (bytes.size() - 2) / 2);
			int added = 0;
			std::size_t pos = 0;
			while (pos < text.size())
			{
				auto eol = text.find(L'\n', pos);
				if (eol == std::wstring::npos) { eol = text.size(); }
				std::wstring line = text.substr(pos, eol - pos);
				pos = eol + 1;
				if (!line.empty() && line.back() == L'\r') { line.pop_back(); }
				if (line.empty() || line[0] != L'$') { continue; }
				const auto tab = line.find(L'\t');
				if (tab == std::wstring::npos) { continue; }
				std::string key = ToUtf8(line.substr(1, tab - 1));
				std::wstring value = line.substr(tab + 1);
				for (std::size_t i = 0; i + 1 < value.size(); ++i)
				{
					if (value[i] == L'\\' && value[i + 1] == L'n') { value.replace(i, 2, L"\n"); }
				}
				if (!a_overwrite && a_out.count(key)) { continue; }
				a_out[key] = ToUtf8(value);
				++added;
			}
			return added;
		}
	}

	void Load()
	{
		const std::string override = Lower(settings::Get().language);
		const std::string lang = override.empty() || override == "auto" ? GameLanguage() : override;

		std::unordered_map<std::string, std::string> texts;
		const int fromLang = ReadInto(FileFor(lang), texts, true);
		int fromEnglish = 0;
		if (lang != "english") { fromEnglish = ReadInto(FileFor("english"), texts, false); }

		std::string all;
		for (const auto& [k, v] : texts) { all += v; all += '\n'; }
		// The atlas text: not just this framework's file but EVERY mod's <Mod>_<language>.txt in the
		// folder (1.6.5), so a consumer page's glyphs are in the atlas without the consumer touching
		// fonts. Read into a scratch map; only the characters matter here.
		int otherFiles = 0;
		{
			std::error_code ec;
			const auto dir = TranslationsDir();
			const std::string suffix = "_" + lang + ".txt";
			if (std::filesystem::is_directory(dir, ec))
			{
				for (const auto& e : std::filesystem::directory_iterator(dir, ec))
				{
					if (!e.is_regular_file(ec)) { continue; }
					const std::string name = Lower(e.path().filename().string());
					if (name.size() <= suffix.size() || name.substr(name.size() - suffix.size()) != suffix) { continue; }
					if (name.rfind("apocryphamenuframework_", 0) == 0) { continue; }
					std::unordered_map<std::string, std::string> other;
					if (ReadInto(e.path(), other, true) > 0)
					{
						++otherFiles;
						for (const auto& [k, v] : other) { all += v; all += '\n'; }
					}
				}
			}
		}

		{
			std::scoped_lock l(g_lock);
			g_texts = std::move(texts);
			g_language = lang;
			g_allText = std::move(all);
		}

		if (fromLang < 0)
		{
			logger::info("strings: no translation file for \"{}\" ({}); {}", lang, FileFor(lang).filename().string(),
						 fromEnglish > 0 ? "the English file is used" : "the compiled English text is used");
		}
		else if (fromLang == -2)
		{
			logger::warn("strings: {} is not UTF-16LE with a BOM - the SKSE translation format - and was ignored", FileFor(lang).filename().string());
		}
		else
		{
			logger::info("strings: {} text(s) read for \"{}\"{} ({}); atlas text also from {} other mod translation file(s)", fromLang, lang,
						 fromEnglish > 0 ? std::format(", {} filled from English", fromEnglish) : "",
						 override.empty() ? "the game's language" : "the INI's sLanguage", otherFiles);
		}
	}

	const char* TR(const char* a_key, const char* a_english)
	{
		std::scoped_lock l(g_lock);
		const auto it = g_texts.find(a_key);
		return it == g_texts.end() ? a_english : it->second.c_str();
	}

	const std::string& Language()
	{
		return g_language;
	}

	std::vector<std::string> Available()
	{
		std::vector<std::string> out;
		std::error_code ec;
		const auto dir = TranslationsDir();
		if (std::filesystem::is_directory(dir, ec))
		{
			for (const auto& e : std::filesystem::directory_iterator(dir, ec))
			{
				if (!e.is_regular_file(ec)) { continue; }
				const std::string name = Lower(e.path().filename().string());
				constexpr const char* prefix = "apocryphamenuframework_";
				if (name.rfind(prefix, 0) != 0 || name.size() <= std::strlen(prefix) + 4 || name.substr(name.size() - 4) != ".txt") { continue; }
				out.push_back(name.substr(std::strlen(prefix), name.size() - std::strlen(prefix) - 4));
			}
		}
		std::sort(out.begin(), out.end());
		out.erase(std::unique(out.begin(), out.end()), out.end());
		const auto en = std::find(out.begin(), out.end(), "english");
		if (en != out.end()) { out.erase(en); }
		out.insert(out.begin(), "english");
		return out;
	}

	void SetLanguage(const std::string& a_language)
	{
		settings::Get().language = Lower(a_language) == "auto" ? "" : Lower(a_language);
		settings::Save();
		Load();
		renderer::RequestFontRebuild();
		logger::info("strings: language set to \"{}\" -> showing \"{}\"", a_language, g_language);
	}

	const std::string& AllText()
	{
		return g_allText;
	}

	std::string GameLanguageSetting()
	{
		return GameLanguage();
	}
}
