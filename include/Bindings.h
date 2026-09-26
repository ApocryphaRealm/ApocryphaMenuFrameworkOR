#pragma once

// ============================================================================================
// THE FRAMEWORK'S OWN CONTROLS, REBINDABLE (the owner, 2026-09-19: "add a tab to the controls row
// to divide controller and keyboard and let them rebind the different functions to different
// buttons/stick/mouse").
//
// Until now every control this framework claims was fixed in code: F1 opened the menu (the one
// rebindable thing), Escape closed it, Start closed it on a pad, and navigation was whatever
// ImGui's own nav keys happened to be. This gives each of them a name, a keyboard/mouse binding
// and a gamepad binding, both settable from the Controls page's two tabs.
//
// WHAT A BINDING MAY BE (the owner's "buttons/stick/mouse"):
//   keyboard side   a keyboard key (DirectInput scan code) OR a mouse button
//   gamepad side    a pad button (XInput mask) OR a stick click (L3/R3, which are just masks)
//                   OR a stick DIRECTION - left stick up, right stick left, and so on
//
// RULES THIS OBEYS, all of them standing ones:
//   * AMF's reserved keys are refused outright (rule: respect-amf-reserved-keys).
//   * Two functions that can be active at the same time may not share a binding; the capture
//     refuses and says which function already has it (rule: bindable-keys-standard-features).
//   * Every function has a keyboard row AND a gamepad row (rule:
//     rebindable-keyboard-and-gamepad-in-every-menu).
//   * The shipped defaults are the ones the framework already used, so nobody's muscle memory
//     changes by updating.
// ============================================================================================

#include <cstdint>
#include <string>
#include <unordered_map>

namespace bindings
{
	// Everything the framework itself claims. A consumer mod's own keys are its business; these
	// are only the menu's.
	enum class Action : int
	{
		kToggleMenu = 0,   // open and close the menu
		kClose,            // close only
		kUp,
		kDown,
		kLeft,
		kRight,
		kActivate,
		kBack,
		kPaneLeft,         // cross from the page back to the mod list
		kPaneRight,        // cross from the mod list into the page
		kOskShift,         // on-screen keyboard: shift
		kOskBackspace,     // on-screen keyboard: backspace
		kOskDone,          // on-screen keyboard: finish
		kTabPrev,          // previous tab on whichever tab bar is innermost
		kTabNext,          // next tab
		kContextMenu,      // open the highlighted mod's context menu (was hard-wired to Y)
		kFavourite,        // favourite/unfavourite the highlighted mod outright
		kCount
	};

	// How a gamepad binding is expressed.
	enum class PadKind : int
	{
		kNone = 0,
		kButton,      // XInput mask, stick clicks included (L3 = 0x40, R3 = 0x80)
		kStickDir     // code = (stick << 4) | direction; stick 0 = left, 1 = right
	};                //        direction 0 = up, 1 = down, 2 = left, 3 = right

	enum class KeyKind : int
	{
		kNone = 0,
		kKeyboard,    // DirectInput scan code
		kMouse        // button index, 0 = left
	};

	struct Binding
	{
		KeyKind keyKind = KeyKind::kNone;
		std::int32_t keyCode = -1;
		PadKind padKind = PadKind::kNone;
		std::int32_t padCode = -1;
	};

	// The action's name, already translated, for the Controls page.
	const char* Label(Action a_action);
	// One line saying what it does, for the row beneath the name.
	const char* Description(Action a_action);

	Binding Get(Action a_action);
	void ResetToDefaults();

	// Clear one side of one binding. An unbound function simply does nothing on that device - the
	// other device keeps whatever it had, so unbinding a key never leaves a controller player
	// without the control (the owner, 2026-09-19: "the controls page should have an unbind button
	// as well if you want to unbind a key").
	void Unbind(Action a_action, bool a_gamepadSide);

	// What the page prints in the button: "F1", "Mouse 3", "A", "Left stick up", "unbound".
	std::string KeyText(Action a_action);
	std::string PadText(Action a_action);

	// ---- capture -------------------------------------------------------------------------
	// Arms the next press of that kind as this action's binding. Escape (keyboard side) and B
	// (gamepad side) cancel. Only one capture at a time.
	void BeginCapture(Action a_action, bool a_gamepadSide);
	void CancelCapture();
	bool IsCapturing();
	Action CapturingAction();
	bool CapturingGamepadSide();
	// Why the last capture was refused, for the page to show. Empty when nothing was refused.
	const char* LastRefusal();

	// ---- the input hook's side -------------------------------------------------------------
	// Each returns true when the event was CONSUMED by an armed capture.
	bool OfferKeyboard(std::uint32_t a_scancode, bool a_down);
	bool OfferMouse(std::uint32_t a_button, bool a_down);
	bool OfferGamepad(std::uint32_t a_mask, bool a_down);
	bool OfferStick(int a_which, float a_x, float a_y);

	// Which action, if any, this input is bound to. kCount means none.
	Action FromKeyboard(std::uint32_t a_scancode);
	Action FromMouse(std::uint32_t a_button);
	Action FromGamepad(std::uint32_t a_mask);
	Action FromStick(int a_which, int a_direction);

	// ---- actions that are not a navigation key ---------------------------------------------
	// Up, Down, Activate and the rest become ImGui keys and ImGui does the work. These four have no
	// ImGui equivalent - they are the framework's own commands - so the input hook RAISES a flag
	// when their binding is pressed and the renderer consumes it on the next frame it draws.
	void RaiseTriggered(Action a_action);
	// Raise every command action bound to this input. Two actions may legitimately share one
	// control when they can never be live together, so asking "which action is this?" is the wrong
	// question - the right one is "which of them is this?".
	void RaiseAllFor(std::uint32_t a_code, bool a_gamepad);
	bool TakeTriggered(Action a_action);

	// ---- INI, in the framework's own file like every other setting -------------------------
	void LoadFrom(const std::unordered_map<std::string, std::string>& a_iniEntries);
	std::string IniBlock();
}
