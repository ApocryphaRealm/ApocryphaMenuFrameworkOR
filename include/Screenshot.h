#pragma once

// ============================================================================================================
// A screenshot key that catches the framework's window (the owner, 2026-10-02: "I'd rather just have a hotkey that
// takes the same screenshot that yours does ... It only needs to run during the game").
//
// Steam's F12 copies the frame inside the game's present call, before this framework draws its window there, so it
// shows the game without the menu. This takes what Windows is SHOWING - a GDI copy of the game's screen area, the same
// picture Claude's capture takes - so the menu, the HUD and any overlay are in it. Nothing about how or when the
// framework draws changes. The copy and the PNG are made on a worker thread, so the game is not held up.
//
// [Screenshot] in the INI: bEnabled, uKey (DirectInput scan code, 88 = F12), bCtrl / bShift / bAlt, sFolder (empty =
// Documents\My Games\Oblivion Remastered\AMF Screenshots).
// ============================================================================================================

namespace screenshot
{
	// The game thread, every frame (main.cpp's OnFrame): watches the key, edge-triggered, while the game is in front.
	void Tick();
}
