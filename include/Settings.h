#pragma once

// ============================================================================================
// M2: the framework's own settings. Plain std::fstream file I/O ONLY - never the Win32 profile
// API (GetPrivateProfileString et al.), which is how the PrivateProfileRedirector stale-cache
// class of bug was born into six of this project's mods at once (fixed 2026-08-27; rule in
// plan.md decided requirement 7). Compiled defaults here MUST match the shipped INI exactly
// (project rule 16) - the shipped file lives at dist/ApocryphaMenuFramework.ini in this repo.
// ============================================================================================

#include <cstdint>
#include <string>

namespace settings
{
	// A REMEMBERED WINDOW GEOMETRY, held as FRACTIONS of the display so it survives a resolution
	// change, a different monitor, or borderless-to-fullscreen.
	//
	// There is one of these per way of opening the framework, and they are PROFILES rather than
	// presets (author, 2026-09-04: "lets have it treat them as profiles to save the users settings
	// to so that we set the default to vanilla positioning and size and if their ui mod does
	// different then they can change it and it will remember"). Each starts unset, which means
	// "use this profile's default" - the measured journal panel when nested, the centre anchor
	// otherwise. The moment the player moves or resizes the window, the result is stored here and
	// that profile stops taking the default. Vanilla's geometry is therefore a starting point, not
	// a cage: a menu replacer whose panel sits somewhere else only needs dragging once.
	struct WindowGeometry
	{
		float x = -1.0f;   // -1 on any field = never set by the player
		float y = -1.0f;
		float w = -1.0f;
		float h = -1.0f;
		// 1.7.8: the KEY-OPENED window keeps only x/y/w/h (a corner drag keeps its shape; nothing scales).
		// 1.8.1: the NESTED profile also remembers which journal ART it was dragged under (the owner,
		// 2026-09-13: the position must only follow the redesign while the redesign is active). A stored
		// position whose art is not the art on screen is ignored and the measured panel is used.
		std::string art;

		bool IsSet() const { return x >= 0.0f && y >= 0.0f && w > 0.0f && h > 0.0f; }
		void Clear() { x = y = w = h = -1.0f; }
	};

	struct Values
	{
		// [Window] - one profile per way in; see WindowGeometry above.
		WindowGeometry nestedWindow;   // opened from the row in the game's System menu
		WindowGeometry hotkeyWindow;   // opened by the hotkey, or by a menu launcher through the API

		// [Input]
		std::int32_t toggleKey = 0x3B;   // DirectInput scan code; 0x3B = F1 (framework convention, the author 2026-08-27)
		// 1.8.9: the on-screen keyboard for controller players (the owner, 2026-09-18) - a key grid
		// across the bottom of the screen that types into whichever text box a page has highlighted.
		// See Keyboard.h. Off hides it entirely; consumer mods can still summon it by export.
		bool onScreenKeyboard = true;    // bOnScreenKeyboard

		// [Menu] bPauseGame (the owner, 2026-09-22: "next amf update gets a toggle in settings to stop time while
		// menu is active"): while this window is open the game is paused the way its own menus pause it - world
		// time, actors, weather and cooldowns stop. OFF by default, so nothing changes for anyone who does not turn
		// it on. Opened from the System row the game is already paused by the journal; this adds nothing there.
		bool pauseGameWhileOpen = false;
		// [Menu] bKeepCameraAwake (the owner, 2026-09-29): while this window is open the game's idle vanity camera never
		// takes over (it rotates the view and hides the HUD, which is what a HUD mod's page is there to show); its own
		// timer is stopped while the window is open and restarted, as after the game's pause menu, when it closes.
		bool keepCameraAwake = true;

		// [Display]
		float textScale = 1.30f;         // extra font multiplier on top of the resolution scale (the author, 1.0.2 feedback round)
		// Optional path to a .ttf to rasterise the menu text from. Empty = pick a clean system
		// face automatically. Set it to use any font, e.g. one that matches Skyrim's own lettering.
		std::string fontPath;
		// [Display] sLanguage - which translation file the framework's own text comes from
		// (Interface\Translations\ApocryphaMenuFramework_<language>.txt). Empty = follow the game's
		// sLanguage. Set from the Language combo on the Framework Settings page or here.
		std::string language;

		// Hang watchdog (the author, 2026-08-28): if the renderer stops producing frames for this many
		// seconds the game is treated as hung and the process terminates itself, so a wedged game
		// never needs Task Manager. Generous by default - a slow cell load still animates frames.
		// The row this framework adds to the GAME's own System menu, so mod settings are reached
		// where a player already looks for configuration rather than from a private hotkey.
		// Injected into the live menu at runtime, so it works over whatever menu artwork is
		// installed and collides with none of it.
		bool systemMenuRow = true;
		bool watchdogEnabled = true;
		// Fast exit (the author, 2026-09-05: "a way to deal with this on exit no kill function issue"):
		// when the game asks Windows to exit, end the process at once instead of running every
		// loaded DLL's and driver's shutdown code - the phase in which a game can wedge into a state
		// no kill, inside or outside the process, can reach. Nothing the game needs happens there.
		bool fastExit = true;
		// [Screenshot] sFolder (1.0.6): where the Screenshot control saves (see Screenshot.h). Empty = the game's
		// Data\AMF Screenshots, which Mod Organizer 2 puts in its overwrite. The control itself is in [Bindings].
		std::string   screenshotFolder;
		// Startup curtain (the owner, 2026-09-15): hold the screen black from the first drawn
		// frame until the game's main menu is up, so the logo frames and the half-drawn menu are
		// never shown. Lifts by itself on a timeout - see Curtain.cpp, where failing safe is the
		// whole design.
		bool startupCurtain = true;
		// How long the curtain may stay up before it gives up and lifts anyway. INI-only, because
		// it is a safety valve rather than a preference. 30 proved too short on a heavy list.
		std::uint32_t curtainTimeoutSeconds = 120;
		// [Startup] sCurtainImage - a picture to show on the curtain instead of plain black, given
		// relative to Data (e.g. SKSE\\Plugins\\ApocryphaMenuFramework\\curtain.png). Empty is the
		// default and means black, so a plain install looks exactly as it did. The image is fitted
		// inside the screen with its aspect kept, on black, and fades out with the curtain.
		std::string curtainImage;
		std::uint32_t watchdogSeconds = 120;
		std::int32_t windowPreset = 0;   // 0 = centre (the standard). Preset positions, never free placement -
		                                 // the author 2026-08-27, same anchor philosophy as the minimap; more presets later.

		// [Theme]
		std::string themeId = "oblivion";     // registry id (theme::Palette::id). Default is Oblivion, the embroidered map edge (2026-10-02)
		                                     // own Skyrim theme for the current test (the author,
		                                     // 2026-08-27) - "Untarnished" (the original identity)
		                                     // is still registered and selectable, just not default.

		// [Skin] - REPLACEMENT ARTWORK for the menu shell, so a UI author can make the framework
		// match their own interface (requested 2026-09-09 for borokoshow / Dragonborn UI). All
		// four are optional and independent; see Skin.h for what an author actually ships.
		// Every image is PNG - AMF decodes through WIC, which does not read DDS at all.
		// OFF BY DEFAULT, and that is the point. Art that is on unless you turn it off means a
		// player with no art replacer installed can end up looking at whatever placeholder PNGs
		// happen to be on disk - which is exactly what happened during development. A UI author
		// turning the feature on is one line; a player seeing art they never asked for is a bug.
		bool          skinEnabled = false; // bEnabled - master switch for everything in [Skin]
		std::string   skinFrame;            // sFrame - nine-slice frame PNG, transparent centre
		std::uint32_t skinFrameCorner = 64; // uFrameCorner - corner slice in px (192x192/64 suggested)
		std::string   skinBackground;       // sBackground - tiled if <= 512px both sides, else stretched
		std::string   skinPlates;           // sPlates - folder holding toggle.png / slider.png / tab.png

		// [Debug]

		// [Log]
		std::int32_t logLevel = 2;       // spdlog level: 2 = info (the shipped default since 2026-09-26; 0 = trace for a bug report)
	};

	// The live values. Read freely from any thread; written by Load() at plugin init and by the
	// settings page on the render thread. Torn reads of a float/int are acceptable here (no
	// value is multi-word), so no lock - matching how every mod in this project treats INI state.
	Values& Get();

	// Reads OBSE/Plugins/ApocryphaMenuFramework.ini (plain file read, file-first - the file
	// is the source of truth, rule 16's persistence half). Missing file or missing key keeps the
	// compiled default and logs which happened. Applies the log level.
	void Load();

	// Rewrites the INI with the current values, comments included, so a settings-page change
	// survives the next game load (rule 16). Logs on failure, never throws.
	void Save();
}
