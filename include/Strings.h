#pragma once

// ============================================================================================
// LANGUAGE SUPPORT (1.6.4, the author: "the foreign language compatible fomod for amf").
//
// Every string the framework itself draws - its Settings, Controls and Help pages and the
// window chrome - goes through TR("key", "English text"). The English text is the compiled
// fallback; the shown text comes from
//     Data\Interface\Translations\ApocryphaMenuFramework_<language>.txt
// the same file shape SkyUI and SKSE use (UTF-16LE with BOM, one "$key<TAB>text" per line), so
// translators already know it and every language file installs beside the others.
//
// Which language: the INI's sLanguage when set ("german", "russian", ...), otherwise the game's
// own sLanguage:General. A file for that language is read; anything it lacks falls back to the
// English file, then to the compiled text - a half-finished translation never shows a raw key.
//
// Consumer mods' pages are theirs to translate; this covers only what the framework draws.
// ============================================================================================

#include <string>
#include <vector>

namespace strings
{
	// Reads the language file(s). Call at kDataLoaded (the game's INI is readable by then) and
	// again after SetLanguage. Safe to call with no file present - English compiled text is used.
	void Load();

	// The text for a key in the active language, UTF-8, or a_english when no file has it.
	// Pointers stay valid until the next Load().
	const char* TR(const char* a_key, const char* a_english);

	// "english", "german", ... - what Load() resolved (after the override and the game's INI).
	const std::string& Language();

	// The game's own sLanguage:General, lower-case ("english" when unset) - the language its item
	// and spell names are in, which can differ from Language() when the INI overrides it (1.8.9).
	std::string GameLanguageSetting();

	// The languages for which a translation file exists on disk, lower-case, sorted - the
	// settings page's combo. "english" is always listed first.
	std::vector<std::string> Available();

	// Sets the INI override ("" = follow the game), saves it, reloads the strings and asks the
	// renderer to rebuild its font atlas for the new glyphs. Also the DevBench "language" op.
	void SetLanguage(const std::string& a_language);

	// Every loaded text concatenated - the font builder feeds it to the glyph-range builder so the
	// atlas holds exactly the characters this language needs (Cyrillic, kana, hanzi, ...).
	const std::string& AllText();
}
