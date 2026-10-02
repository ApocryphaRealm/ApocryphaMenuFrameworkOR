#pragma once

#include <string>

struct ImDrawList;

// ============================================================================================
// M1: the render loop. Two trampoline call-hooks (survey §7.1), a probed D3D-init site
// (Offsets.h explains the dispute), one ImGui context for the whole process.
//
// Hook installation happens in SKSEPluginLoad because renderer bring-up PRECEDES kDataLoaded -
// there is no later moment that still catches init. This is the established pattern across the
// surveyed ecosystem. It is in deliberate tension with this project's DEM crash lesson (avoid
// early relocation work); the mitigations are the byte-pattern guards - a site that does not
// look like a call instruction is never written, the framework logs why and stays inert, and
// the game boots untouched.
// ============================================================================================

namespace renderer
{
	// What the last font atlas build holds (1.8.9): the languages it was built for, its glyph count
	// and size, and whether one probe glyph per script is present. Read by the driving tool.
	struct FontProbe
	{
		std::string language, gameLanguage;
		int glyphs = 0, atlasWidth = 0, atlasHeight = 0, builds = 0;
		bool hasKana = false, hasHangul = false, hasHanzi = false, hasCyrillic = false;
	};
	FontProbe GetFontProbe();

	// Oblivion Remastered: the D3D12 overlay (Overlay.cpp) finds the device and calls these two. OnDeviceReady once,
	// with the game window and back-buffer size (false = the framework stays inert, logged); OnFrame every presented
	// frame, which runs the whole ImGui frame up to and including ImGui::Render().
	bool OnDeviceReady(void* a_hwnd, unsigned a_width, unsigned a_height);
	void OnFrame();

	// The framework window's visibility toggle - flipped by the M1 input sink, consumed by the
	// present thunk. Atomic: touched from the input thread, read on the render thread.
	void ToggleMainWindow();
	bool IsMainWindowVisible();

	// TRUE while an ImGui text field has the keyboard (io.WantTextInput), sampled once per frame.
	// The input hook reads it on the game thread to turn the engine's own text entry on and off -
	// without that the engine makes no CharEvent at all and every text box in the framework is
	// deaf (the search bar report, phbd01 2026-09-19).
	bool WantsTextInput();
	// The game's window handle (HWND) as seen at D3DInit; null before the renderer is up.
	void* GetGameWindow();

	// External menu control + query, used by the DevBench tool (DevBenchTool.cpp) so the menu can
	// be driven and inspected headlessly for testing (rule 31): open/close, move the selection to a
	// node path (e.g. "system/quit", "mod" with the mod set via the tool, "stats"), run the
	// selected node's action, and read the whole menu as JSON. Thread-safe.
	// a_nested says the surface was opened from the row in the GAME's own System menu rather than
	// from the hotkey or a menu launcher. It only affects GEOMETRY: a nested window is sized and
	// placed to the journal panel hosting it, so it reads as a page of that menu instead of a
	// larger window on top of it. Everything it draws is identical either way.
	// A page tells the framework about its OWN tab bar and reads back the tab the D-pad asked for
	// (-1 = nothing). Call it once per frame from the page's render function. A page that never
	// calls it behaves exactly as before, so no other author has to change anything.
	int DeclareInnerTabs(int a_count, int a_current);

	void SetMenuVisible(bool a_visible, bool a_nested = false);
	void SetSelectedNode(const std::string& a_node);
	std::string GetSelectedNode();
	void ActivateSelectedNode();
	// Menu-shell personalization, driven from DevBench for testing (the settings page drives the
	// same personalization:: calls directly). Return false when no such mod is registered.
	// Switch the active theme by registry id (e.g. "skyrim", "untarnished") and
	// save it. Exposed for DevBench so a visual comparison of two themes can be photographed in
	// ONE game session instead of one launch per theme. Returns false for an unknown id.
	bool SetTheme(const std::string& a_themeId);

	// Driving the pane and tab navigation from DevBench (amf.menu op=nav / op=focus), so the
	// controller scheme can be exercised with no keypress at all (rule 64). QueueNav queues a
	// synthetic left/right press that is read in the same place a real D-pad press is read -
	// including the tab walking, so a test proves the shipped decision rather than a private path
	// around it. FocusPane puts nav in the mod list or the options pane, which is how a nav case
	// is set up headlessly. Both return false, and log why, for a value they do not recognise.
	// Thread-safe: called on devbench's listener thread, applied on the render thread.
	bool QueueNav(const std::string& a_direction);  // "left" | "right"
	bool FocusPane(const std::string& a_pane);      // "list" | "options"

	bool SetModAlias(const std::string& a_modName, const std::string& a_alias);
	bool MoveModTo(const std::string& a_modName, int a_position);
	void ResetModOrder();
	// separators (2026-10-02): action add {name, mod = above which} | remove {separator} | send {mod, separator ("" = none)}
	// | collapse {separator} | favourite {separator}; returns the tool's JSON answer
	std::string SeparatorOp(const std::string& a_action, const std::string& a_name, const std::string& a_mod, const std::string& a_separator);

	std::string GetMenuStateJson();
	// 1.8.9: draw the active theme's frame around a rect on a consumer's draw list (see Renderer.cpp).
	bool DrawThemeFrameAround(ImDrawList* a_drawList, float a_x0, float a_y0, float a_x1, float a_y1);

	// In-process capture (ported from the Overhaul line, 2026-08-30): saves the NEXT presented
	// frame - WITH the ImGui overlay - as a PNG at a_path. Blocks the calling (DevBench listener)
	// thread until the render thread serviced it or a_timeoutMs passed. Empty return = success.
	std::string CaptureBlocking(const std::wstring& a_path, unsigned a_timeoutMs);
}
