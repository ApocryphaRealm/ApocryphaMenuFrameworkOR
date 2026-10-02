#include "Theme.h"

#include "KnotworkBorder.h"
#include "MapEdgeBorder.h"

#include "Settings.h"
#include "Logger.h"

#include <imgui.h>

#include <charconv>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>
#include <unordered_map>

namespace theme
{
	namespace
	{
		std::vector<Palette> g_themes;
		std::string g_activeId;

		std::string_view Trim(std::string_view a_text)
		{
			while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t')) a_text.remove_prefix(1);
			while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t' || a_text.back() == '\r')) a_text.remove_suffix(1);
			return a_text;
		}

		bool ParseColor(std::string_view a_hex, std::uint32_t& a_out)
		{
			// Accepts "RRGGBB" (as written in a theme file); stored/consumed as ABGR to match
			// the rest of this codebase's packed-colour convention.
			if (!a_hex.empty() && a_hex.front() == '#')
			{
				a_hex.remove_prefix(1);
			}
			// RRGGBBAA as well (1.9.8), so a theme can ask for a slightly translucent panel the way
			// Vel'dun UI's own ImGui style does (#1D1A17F4).
			if (a_hex.size() != 6 && a_hex.size() != 8)
			{
				return false;
			}

			std::uint32_t value = 0;
			const auto result = std::from_chars(a_hex.data(), a_hex.data() + a_hex.size(), value, 16);
			if (result.ec != std::errc{})
			{
				return false;
			}

			std::uint32_t a = 0xFF;
			if (a_hex.size() == 8)
			{
				a = value & 0xFF;
				value >>= 8;
			}
			const std::uint32_t r = (value >> 16) & 0xFF;
			const std::uint32_t g = (value >> 8) & 0xFF;
			const std::uint32_t b = value & 0xFF;
			a_out = (a << 24) | (b << 16) | (g << 8) | r;  // ABGR
			return true;
		}

		// Skyrim read fHUDOpacity from its INI collections (ported from Dragon's Eye Minimap). Oblivion Remastered
		// keeps its HUD opacity in Unreal's config, which is not wired yet, so the framework renders fully opaque -
		// the same outcome Skyrim had when no candidate name matched.
	}

	void RegisterTheme(Palette a_palette)
	{
		for (Palette& existing : g_themes)
		{
			if (existing.id == a_palette.id)
			{
				logger::info("theme \"{}\" ({}) replaced (re-registration)", a_palette.name, a_palette.id);
				existing = std::move(a_palette);
				return;
			}
		}

		logger::info("theme \"{}\" ({}) registered ({} theme(s) total)", a_palette.name, a_palette.id, g_themes.size() + 1);
		g_themes.push_back(std::move(a_palette));
	}

	void ScanUserThemes()
	{
		constexpr const char* kDir = "OBSE/Plugins/ApocryphaMenuFramework/themes";

		std::error_code ec;
		if (!std::filesystem::exists(kDir, ec) || ec)
		{
			logger::debug("theme scan: {} does not exist; only built-in themes are available", kDir);
			return;
		}

		std::size_t found = 0;

		for (const auto& entry : std::filesystem::directory_iterator(kDir, ec))
		{
			if (ec || !entry.is_regular_file())
			{
				continue;
			}
			if (entry.path().extension() != ".ini")
			{
				continue;
			}

			std::ifstream file(entry.path());
			if (!file.is_open())
			{
				logger::warn("theme scan: could not open {}", entry.path().string());
				continue;
			}

			Palette palette{};
			palette.id = entry.path().stem().string();
			palette.name = palette.id;
			palette.background = 0xFF000000;
			palette.frame = 0xFFE9F2F5;
			palette.borderThickness = 1.0f;

			std::string line;
			while (std::getline(file, line))
			{
				const std::string_view trimmed = Trim(line);
				if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#' || trimmed.front() == '[')
				{
					continue;
				}
				const auto equals = trimmed.find('=');
				if (equals == std::string_view::npos)
				{
					continue;
				}
				const std::string_view key = Trim(trimmed.substr(0, equals));
				const std::string_view value = Trim(trimmed.substr(equals + 1));

				if (key == "sName")
				{
					palette.name = std::string(value);
				}
				else if (key == "sBackground")
				{
					ParseColor(value, palette.background);
				}
				else if (key == "sFrame")
				{
					ParseColor(value, palette.frame);
				}
				else if (key == "fBorderThickness")
				{
					float v{};
					if (std::from_chars(value.data(), value.data() + value.size(), v).ec == std::errc{})
					{
						palette.borderThickness = v;
					}
				}
				// 1.9.8: the refined colour roles and the theme's own art, so an INI theme can be as
				// complete as a built-in one. Every key is optional; an absent one keeps the fallback.
				else if (key == "sBorder") { ParseColor(value, palette.border); }
				else if (key == "sText") { ParseColor(value, palette.text); }
				else if (key == "sTextDim") { ParseColor(value, palette.textDim); }
				else if (key == "sAccent") { ParseColor(value, palette.accent); }
				else if (key == "bKnotwork") { palette.knotwork = (value == "1" || value == "true"); }
				else if (key == "bMapEdge") { palette.mapEdge = (value == "1" || value == "true"); if (palette.mapEdge) { palette.knotwork = true; } }
				else if (key == "sSkinFrame") { palette.skinFrame = std::string(value); }
				else if (key == "sSkinBackground") { palette.skinBackground = std::string(value); }
				else if (key == "sSkinPlates") { palette.skinPlates = std::string(value); }
				else if (key == "uSkinFrameCorner")
				{
					std::uint32_t v{};
					if (std::from_chars(value.data(), value.data() + value.size(), v).ec == std::errc{} && v > 0)
					{
						palette.skinFrameCorner = v;
					}
				}
			}

			RegisterTheme(std::move(palette));
			++found;
		}

		logger::info("theme scan: {} user theme(s) loaded from {}", found, kDir);
	}

	std::vector<Palette> ListThemes()
	{
		return g_themes;
	}

	// The 2026-09-01 merge: "vanilla" and "mo2-skyrim" both became "skyrim". An INI written
	// before that names a theme that no longer exists, so map the retired ids rather than
	// silently falling back and losing the player's choice.
	std::string MigrateThemeId(const std::string& a_id)
	{
		if (a_id == "vanilla" || a_id == "mo2-skyrim") { return "skyrim"; }
		// 2026-10-02: the map-edge theme's first name, and the local Oblivion Paper theme it replaced (the owner: "rename
		// the current theme to Oblivion, and we can get rid of the old Oblivion paper theme").
		if (a_id == "cyrodiil" || a_id == "oblivion-paper") { return "oblivion"; }
		return a_id;
	}

	void SetActiveTheme(const std::string& a_id)
	{
		if (a_id.empty())
		{
			return;
		}

		for (const Palette& p : g_themes)
		{
			if (p.id == a_id)
			{
				g_activeId = a_id;
				logger::info("active theme -> \"{}\" ({})", p.name, p.id);
				return;
			}
		}

		logger::warn("SetActiveTheme(\"{}\") refused: no such theme registered", a_id);
	}

	const Palette& GetActiveTheme()
	{
		for (const Palette& p : g_themes)
		{
			if (p.id == g_activeId)
			{
				return p;
			}
		}

		// Fall back to whatever registered first (the compiled-in default) rather than crash -
		// this only happens if g_activeId was never set to a real id, which Apply()'s caller
		// prevents by registering built-ins before ever calling this.
		return g_themes.front();
	}

	float GetGameHUDOpacity()
	{
		return 1.0f;   // see the note above GetGameHUDOpacity's Skyrim source: not wired on Oblivion Remastered yet
	}

	void Apply()
	{
		if (g_themes.empty())
		{
			// Built-ins, registered here rather than at a separate call site so Apply() is
			// always safe to call standalone (e.g. from a test/DevBench path).
			//
			// "Untarnished" - the ORIGINAL identity (solid black, #F5F2E9 warm off-white),
			// shipped as a selectable theme per the author's instruction, no longer the only option.
			RegisterTheme({ "untarnished", "Untarnished", 0xFF000000, 0xFFE9F2F5, 1.0f });

			// "Skyrim" - the knotwork look: the Nordic frame art and silver/gold lines rebuilt from
			// the real Trosski Skyrim style (its border-image.png and stylesheet), with the crisper
			// warm off-white text of the Untarnished palette. It replaces the old "Vanilla" and
			// "MO2 Skyrim" entries, which the author retired on 2026-09-01 as "extremely similar in
			// colour and design" - they differed only in the text tone, and this keeps the better one.
			// Silver frame lines #b0b0b0, dim secondary #717171, GOLD accent #a1912b for selection,
			// text #F5F2E9. ABGR packing (0xAABBGGRR). Background stays solid black: the project's
			// full-opacity rule holds over live gameplay, unlike MO2's near-transparent desktop look.
			RegisterTheme({ "skyrim", "Skyrim",
				/*background*/ 0xFF000000, /*frame*/ 0xFFB0B0B0, /*borderThickness*/ 1.0f,
				/*border*/ 0xFFB0B0B0, /*text*/ 0xFFE9F2F5, /*textDim*/ 0xFF717171,
				/*accent*/ 0xFF2B91A1, /*knotwork*/ true });

			// "Oblivion" (named "Cyrodiil Map" for an hour; the owner: "just rename the current theme to
			// Oblivion") - this framework's OWN look and its default (the owner, 2026-10-02: "give it a frame art
			// similar to how Skyrim has a frame art, except this frame will be more like an embroidered map's edge ...
			// in a gold or brown color"). The embroidered map-edge frame (MapEdgeBorder.h, original art) on a
			// parchment ground with brown ink: #E4DBCC paper, #2A1C12 text, #6F5D4C dim, #6B563F lines, #8A6A2C
			// brass accent - the game's paper-menu palette as plain colours, so no game art ships. ABGR packing.
			RegisterTheme({ "oblivion", "Oblivion",
				/*background*/ 0xFFCCDBE4, /*frame*/ 0xFF161C24, /*borderThickness*/ 1.0f,
				/*border*/ 0xFF3F566B, /*text*/ 0xFF121C2A, /*textDim*/ 0xFF4C5D6F,
				/*accent*/ 0xFF2C6A8A, /*knotwork*/ true, /*mapEdge*/ true });

			g_activeId = "oblivion";

			ScanUserThemes();

			// Honour a saved preference from the INI (file-first, rule 16) if it names a real
			// theme; otherwise the compiled default above stands. Only checked on this first
			// call - the live picker calls SetActiveTheme() itself before every later Apply().
			SetActiveTheme(settings::Get().themeId);
		}
		else
		{
			ScanUserThemes();
		}

		const Palette& active = GetActiveTheme();

		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowBorderSize = active.borderThickness;
		style.FrameBorderSize = active.borderThickness;
		style.PopupBorderSize = active.borderThickness;
		style.ChildBorderSize = active.borderThickness;

		// MARGINS FOR THE KNOTWORK ART (author, 2026-09-01, comparing Vanilla against Untarnished:
		// "the Skyrim theme doesn't let them fully see all the corners and lines of a box with a
		// border ... you might have to change the margin between those areas and the edge of the
		// menu frame"). The knotwork frame is a 9-slice drawn ON a rect, and its corner ornament is
		// a FIXED 26px regardless of UI scale, so it occupies a 26px band just inside whatever rect
		// it frames. With ImGui's default padding (8px, ~13px after the resolution scale) the
		// window's band lay over the panes inside it and each pane's band lay over its own first
		// line of text - which is why the version line read "pocrypha Menu Framework". Themes
		// WITHOUT the art keep ImGui's normal padding; themes with it get the band's width plus a
		// few pixels of air, so every box's border and all four corners stay visible.
		// EVERY theme gets the same padding (author, 2026-09-01: "edit the untarnished theme to have
		// the same margin edits so they look similar in spacing"). The knotwork art is what forced
		// the figure - its corner ornament is a fixed 26px band inside whatever rect it frames - but
		// applying it to the plain themes too keeps the layout identical whichever theme is picked,
		// so switching theme changes the colours and the art, never the geometry.
		// The map-edge frame (2026-10-02) was drawn to the same 26 px corner, so it shares this padding.
		static_assert(mapedge::kDrawCorner == knotwork::kCorner, "every built-in frame shares one corner, so one layout");
		constexpr float kFramePadding = static_cast<float>(knotwork::kCorner) + 8.0f;
		style.WindowPadding = ImVec2(kFramePadding, kFramePadding);
		style.TabBorderSize = active.borderThickness;
		style.WindowRounding = 0.0f;
		style.FrameRounding = 0.0f;

		auto unpack = [](std::uint32_t abgr) {
			const float a = ((abgr >> 24) & 0xFF) / 255.0f;
			const float b = ((abgr >> 16) & 0xFF) / 255.0f;
			const float g = ((abgr >> 8) & 0xFF) / 255.0f;
			const float r = (abgr & 0xFF) / 255.0f;
			return ImVec4{ r, g, b, a };
		};

		// A theme provides `frame`; the refined roles (border/text/textDim/accent) fall back to
		// it when 0, so simple and INI-scanned themes are unchanged while MO2 Skyrim gets its
		// real layered look.
		const std::uint32_t borderId = active.border ? active.border : active.frame;
		const std::uint32_t textId = active.text ? active.text : active.frame;
		const std::uint32_t textDimId = active.textDim ? active.textDim : active.frame;
		const std::uint32_t accentId = active.accent ? active.accent : active.frame;

		const ImVec4 black = unpack(active.background);
		const ImVec4 border = unpack(borderId);
		const ImVec4 text = unpack(textId);
		const ImVec4 accent = unpack(accentId);

		// Alpha variants of a base colour, for the graded hover/active/fill states.
		auto tint = [](const ImVec4& v, float a) { return ImVec4{ v.x, v.y, v.z, a }; };
		const ImVec4 textDimC = tint(unpack(textDimId), 1.0f);           // secondary text (its own hue)
		const ImVec4 borderDim = tint(border, 0.55f);                    // separators
		const ImVec4 borderFaint = tint(border, 0.14f);                  // subtle fills
		const ImVec4 borderSoft = tint(border, 0.28f);                   // hover fills
		const ImVec4 accentFaint = tint(accent, 0.22f);                  // selected row (gold wash)
		const ImVec4 accentSoft = tint(accent, 0.42f);                   // hovered/active selection

		ImVec4* c = style.Colors;
		c[ImGuiCol_WindowBg] = black;
		c[ImGuiCol_ChildBg] = black;
		c[ImGuiCol_PopupBg] = black;
		c[ImGuiCol_MenuBarBg] = black;
		c[ImGuiCol_TitleBg] = black;
		c[ImGuiCol_TitleBgActive] = black;
		c[ImGuiCol_TitleBgCollapsed] = black;

		c[ImGuiCol_Text] = text;
		c[ImGuiCol_TextDisabled] = textDimC;  // NOT ImGui's ~50% grey - the readability rule applies to every theme

		c[ImGuiCol_Border] = border;
		c[ImGuiCol_BorderShadow] = ImVec4{ 0, 0, 0, 0 };
		c[ImGuiCol_Separator] = borderDim;
		c[ImGuiCol_SeparatorHovered] = borderSoft;
		c[ImGuiCol_SeparatorActive] = border;

		c[ImGuiCol_FrameBg] = black;
		c[ImGuiCol_FrameBgHovered] = borderFaint;
		c[ImGuiCol_FrameBgActive] = borderSoft;
		c[ImGuiCol_Button] = black;
		c[ImGuiCol_ButtonHovered] = borderFaint;
		c[ImGuiCol_ButtonActive] = borderSoft;

		// Selection (Selectable, tree, list rows) = the gold accent wash - the Skyrim warmth.
		c[ImGuiCol_Header] = accentFaint;
		c[ImGuiCol_HeaderHovered] = accentSoft;
		c[ImGuiCol_HeaderActive] = accentSoft;

		// Tabs: quiet by default, gold when active/selected.
		c[ImGuiCol_Tab] = black;
		c[ImGuiCol_TabHovered] = accentSoft;
		c[ImGuiCol_TabActive] = accentFaint;
		c[ImGuiCol_TabUnfocused] = black;
		c[ImGuiCol_TabUnfocusedActive] = borderFaint;

		// Scrollbar: dark trough, silver grab.
		c[ImGuiCol_ScrollbarBg] = black;
		c[ImGuiCol_ScrollbarGrab] = borderDim;
		c[ImGuiCol_ScrollbarGrabHovered] = borderSoft;
		c[ImGuiCol_ScrollbarGrabActive] = border;

		// Interactive accents in gold.
		c[ImGuiCol_SliderGrab] = accent;
		c[ImGuiCol_SliderGrabActive] = accent;
		c[ImGuiCol_CheckMark] = accent;
		// The controller navigation box is bright blue in every theme (the owner, 2026-09-15: "the next amf version should have
		// a bright blue controller nav box instead of the old yellow one"), so the focused item stands out from the gold
		// selection wash instead of blending into it.
		c[ImGuiCol_NavHighlight] = ImVec4{ 0.24f, 0.62f, 1.00f, 1.00f };
		// 1.7.7: every remaining ImGui default that is blue or off-palette (the owner saw "a bit more of a
		// blue or purple colour in some areas"); nothing the menu draws is left on Dear ImGui's own palette.
		c[ImGuiCol_TextSelectedBg] = accentSoft;
		c[ImGuiCol_DragDropTarget] = accent;
		c[ImGuiCol_ResizeGrip] = borderFaint;
		c[ImGuiCol_ResizeGripHovered] = borderSoft;
		c[ImGuiCol_ResizeGripActive] = border;
		c[ImGuiCol_TableHeaderBg] = black;
		c[ImGuiCol_TableBorderStrong] = borderDim;
		c[ImGuiCol_TableBorderLight] = borderFaint;
		c[ImGuiCol_TableRowBg] = ImVec4{ 0, 0, 0, 0 };
		c[ImGuiCol_TableRowBgAlt] = borderFaint;
		c[ImGuiCol_PlotLines] = accent;
		c[ImGuiCol_PlotLinesHovered] = accentSoft;
		c[ImGuiCol_PlotHistogram] = accent;
		c[ImGuiCol_PlotHistogramHovered] = accentSoft;
		c[ImGuiCol_ModalWindowDimBg] = ImVec4{ 0, 0, 0, 0.6f };
		c[ImGuiCol_NavWindowingHighlight] = accent;
		c[ImGuiCol_NavWindowingDimBg] = ImVec4{ 0, 0, 0, 0.4f };

		logger::info("Theme applied: \"{}\" ({}); frame={}; game HUD opacity {:.2f}",
					 active.name, active.id, !active.knotwork ? "none" : (active.mapEdge ? "map edge" : "knotwork"),
					 GetGameHUDOpacity());
	}
}
