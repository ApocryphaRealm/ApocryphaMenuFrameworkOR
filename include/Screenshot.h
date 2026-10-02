#pragma once

// ============================================================================================================
// A screenshot control that catches the framework's window (the owner, 2026-10-02: "I'd rather just have a hotkey that
// takes the same screenshot that yours does", then "make it a rebindable option ... it can be something that only works
// with AMF open because regular Steam screenshot works in the game itself").
//
// Steam's F12 copies the frame inside the game's present call, before this framework draws its window there, so it
// shows the game without the menu. This takes what Windows is SHOWING - a GDI copy of the game's screen area, the same
// picture Claude's capture takes - so the menu is in it. Nothing about how or when the framework draws changes. The copy
// and the PNG are made on a worker thread, so the game is not held up.
//
// The control is bindings::Action::kScreenshot (Controls page, press-to-bind; F11 / View by default), raised only while
// the menu is open. The pictures go to the game's Data\AMF Screenshots - under Mod Organizer 2 that is a new file in a
// virtual folder, so it lands in MO2's overwrite (the owner: "have it kicked out through MO2 into the overwrite"). The
// INI's [Screenshot] sFolder names another folder instead.
// ============================================================================================================

namespace screenshot
{
	// Copies the game's screen area now and saves it on a worker thread. A rising double beep says it was saved.
	void Take();
}
