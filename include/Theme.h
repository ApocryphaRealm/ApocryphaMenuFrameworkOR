#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ============================================================================================
// Theme REGISTRY (the author, 2026-08-27) - supersedes the original "no theme-customisation UI by
// design" stance recorded below, the same way a rule gets amended rather than left standing
// alongside its own contradiction (project convention: fold the update into the decision).
//
// Mirrors MO2's own theme mechanism exactly, confirmed by reading it directly
// (C:\Modlists\Apostasy\stylesheets\*.qss - one self-contained file per theme, additive,
// nothing overwrites anything else). AMF's equivalent: a Palette per theme, registered either
// at compile time (the two built-ins below) or scanned additively from
// OBSE/Plugins/ApocryphaMenuFramework/themes/*.ini at load - never mutating another
// theme's entry.
//
// Original spec this supersedes, kept for provenance:
//   "the same build philosophy as the Untarnished UI minimap edit - solid black at full
//    opacity, affected by the game's opacity setting, and the white framing around all the
//    buttons and menus." That look now SHIPS AS the "Untarnished" theme (the author's instruction),
//    one entry in the registry rather than the only possible one.
// ============================================================================================

namespace theme
{
	struct Palette
	{
		std::string id;     // stable key, used in the INI (never renamed once shipped)
		std::string name;   // display name in the theme picker

		std::uint32_t background;    // ABGR
		std::uint32_t frame;         // ABGR - primary border/text/accent colour (the ONE colour
		                             // a simple/INI theme provides; the fields below refine it)
		float borderThickness = 1.0f;

		// Optional refinements (2026-08-28, MO2-Skyrim rebuild). A real UI is not one flat colour:
		// the Trosski Skyrim style uses silver frame lines, brighter text, a dim secondary tone
		// and a gold accent. Any left 0 falls back to `frame`, so the two built-ins and every
		// INI-scanned theme keep working unchanged. ABGR, same packing as `frame`.
		std::uint32_t border = 0;    // panel/frame lines            (Trosski #b0b0b0 silver)
		std::uint32_t text = 0;      // primary text                 (Trosski #dddddd bright)
		std::uint32_t textDim = 0;   // disabled/secondary text      (Trosski #717171)
		std::uint32_t accent = 0;    // selection / checkmark / grab (Trosski #a1912b gold)

		// Draw the Nordic knotwork frame (border-image.png, embedded) around the window. The
		// Trosski MO2 style's defining feature; off for plain themes like Untarnished.
		bool knotwork = false;

		// The theme's own ART (1.9.8, the owner, 2026-09-25: AMF themes built on Vel'dun UI and
		// Oathvein UI). A theme in this project means replacement art, so a theme INI may name the
		// same pieces [Skin] does - frame, background, control plates - and they draw whenever that
		// theme is picked. Paths resolve under Data like [Skin]'s. Empty = the theme has no art of
		// that kind. A player's own [Skin] art, when switched on, still wins over these.
		std::string   skinFrame;
		std::uint32_t skinFrameCorner = 64;
		std::string   skinBackground;
		std::string   skinPlates;
	};

	// Registers a theme additively. Re-registering an existing id REPLACES that entry only
	// (never touches any other) - the same semantics as dropping a new .qss into MO2's folder
	// alongside the others.
	void RegisterTheme(Palette a_palette);

	// Scans OBSE/Plugins/ApocryphaMenuFramework/themes/*.ini and registers each as an
	// additional theme. Missing folder is not an error - the two built-ins still work.
	void ScanUserThemes();

	std::vector<Palette> ListThemes();

	// Empty/unknown id is a no-op (current theme stays active); logs either way.
	// Maps the ids retired in the 2026-09-01 theme merge ("vanilla", "mo2-skyrim") onto "skyrim";
	// anything else is returned unchanged. Called when reading a theme id out of the INI.
	std::string MigrateThemeId(const std::string& a_id);

	void SetActiveTheme(const std::string& a_id);
	const Palette& GetActiveTheme();

	// The game's own opacity, applied as ONE global alpha multiplier over the whole theme at
	// draw time - never threaded through individual colour constants. Orthogonal to which
	// palette is active.
	//
	// Resolution strategy is ported from Dragon's Eye Minimap (MiniMap.cpp GetHUDOpacitySetting,
	// proven in game 2026-08-27): try "fHUDOpacity", "fHUDOpacity:MAIN", "fHUDOpacity:Interface",
	// "fHUDOpacity:Display" across BOTH INI setting collections, log which name resolved, and
	// re-read every frame the menu is open so an options-menu change is followed without a
	// reload. FULL OPACITY IS THE FALLBACK - never fail toward translucent.
	float GetGameHUDOpacity();

	// Applies the active theme's palette + border rules to the live ImGui style.
	void Apply();
}
