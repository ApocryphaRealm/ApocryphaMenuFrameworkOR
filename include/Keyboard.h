#pragma once

// ============================================================================================
// THE ON-SCREEN KEYBOARD (1.8.9, the owner, 2026-09-18).
//
// A controller player cannot type into a search box. This is a key grid drawn across the bottom
// of the SCREEN - bound to the screen, not to the framework window, so it never covers the page
// it is typing into and does not move or shrink with the window - that types into whichever
// text field a consumer page has focused. Every mod that draws through the framework gets it
// without writing anything: the framework exports igInputText and its siblings, so it sees every
// text field a page draws and knows which item the controller's highlight is on.
//
// How it works, in the player's terms:
//   * highlight a text box with the D-pad or stick and press A: the box starts taking input and
//     the keyboard appears; the D-pad now walks the keys and A types one (the page's own
//     navigation is held off while the keyboard has the pad);
//   * B (Circle) puts the controller's highlight back on the box and gives the pad back to the
//     page, so the search box, the results and the keys are one short hop apart;
//   * Space, Back, Clear, Shift and Done sit on the bottom row.
//
// A mod with its own text field can also summon it: AMF_ShowKeyboard() / AMF_HideKeyboard().
// The toggle on the framework's Settings page (bOnScreenKeyboard) turns the whole thing off.
// ============================================================================================

#include <cstdint>
#include <string>

namespace keyboard
{
	// Called by the framework's igInputText / WithHint / Multiline exports, right after the item
	// was submitted, so the keyboard knows which ImGui items are text fields this frame.
	// Is this item id one of the text fields seen this frame or last? Used by the renderer's
	// keyboard-loss diagnostic, so a field losing focus is reported as a field rather than as an
	// anonymous id.
	bool IsTextField(std::uint32_t a_id);

	// Was this field SUBMITTED on the frame that just ended? IsTextField answers "this frame or
	// last"; this one is exact, and it is what separates "the widget stopped being drawn" from
	// "something took the focus off it" - two faults that look identical on screen.
	bool WasSubmittedLastFrame(std::uint32_t a_id);

	void NoteTextField(std::uint32_t a_itemId);

	// Render thread, inside the frame, after the framework window (so this frame's text fields
	// are known). Decides whether the keyboard is open and draws it.
	void Draw();

	// True while the keyboard owns the D-pad / A / B: the input translation routes gamepad
	// records here instead of into ImGui's navigation.
	bool Capturing();

	// Render thread (the translation step runs there, before NewFrame). Return true = consumed.
	bool HandleGamepad(std::uint32_t a_xinputMask, bool a_down);
	bool HandleStick(std::uint32_t a_stick, float a_x, float a_y);   // 0 = left (moves the cursor), 1 = right (swallowed while open)

	// The exports' backing: open for the focused text field (or the next one focused), close.
	void Show();
	void Hide();

	// For the DevBench state: {"open":..,"capturing":..,"target":..,"cursor":[r,c],"text":".."}
	std::string StateJson();
}
