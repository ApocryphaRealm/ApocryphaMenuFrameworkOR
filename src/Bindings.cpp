#include "Bindings.h"

#include "Compat.h"

#include "Settings.h"
#include "Strings.h"
#include "Logger.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <sstream>
#include <cstdio>
#include <cstdlib>

// The framework's own reserved-key list, exported for consumers; asked here rather than kept in a
// second place that could disagree with it (standing rule: respect AMF's reserved keys).
extern "C" std::uint32_t SMF_GetReservedKeyCodes(std::int32_t* a_buffer, std::uint32_t a_capacity);


namespace bindings
{
	using strings::TR;

	namespace
	{
		std::mutex g_lock;
		std::array<Binding, static_cast<std::size_t>(Action::kCount)> g_bindings{};

		bool g_capturing = false;
		Action g_captureAction = Action::kCount;
		bool g_captureGamepad = false;
		std::string g_refusal;

		constexpr std::uint32_t kDIKEscape = 0x01;
		constexpr std::uint32_t kPadB = 0x2000;

		// The defaults are exactly what the framework used before any of this existed, so an
		// update changes nobody's muscle memory.
		void SetDefaultsLocked()
		{
			auto set = [](Action a, KeyKind kk, std::int32_t kc, PadKind pk, std::int32_t pc) {
				g_bindings[static_cast<std::size_t>(a)] = Binding{ kk, kc, pk, pc };
			};
			set(Action::kToggleMenu,   KeyKind::kKeyboard, 0x3B, PadKind::kNone,     -1);       // F1
			set(Action::kClose,        KeyKind::kKeyboard, 0x01, PadKind::kButton,   0x0010);   // Escape / Start
			set(Action::kUp,           KeyKind::kKeyboard, 0xC8, PadKind::kButton,   0x0001);   // Up arrow / D-pad up
			set(Action::kDown,         KeyKind::kKeyboard, 0xD0, PadKind::kButton,   0x0002);
			set(Action::kLeft,         KeyKind::kKeyboard, 0xCB, PadKind::kButton,   0x0004);
			set(Action::kRight,        KeyKind::kKeyboard, 0xCD, PadKind::kButton,   0x0008);
			set(Action::kActivate,     KeyKind::kKeyboard, 0x1C, PadKind::kButton,   0x1000);   // Enter / A
			set(Action::kBack,         KeyKind::kKeyboard, 0x0E, PadKind::kButton,   0x2000);   // Backspace / B
			set(Action::kPaneLeft,     KeyKind::kNone,       -1, PadKind::kStickDir, 0x02);     // left stick left
			set(Action::kPaneRight,    KeyKind::kNone,       -1, PadKind::kStickDir, 0x03);     // left stick right
			set(Action::kOskShift,     KeyKind::kNone,       -1, PadKind::kButton,   0x4000);   // X
			set(Action::kOskBackspace, KeyKind::kNone,       -1, PadKind::kButton,   0x8000);   // Y
			set(Action::kOskDone,      KeyKind::kNone,       -1, PadKind::kButton,   0x0020);   // Back
			// The bumpers walk the tabs (the owner, 2026-09-19). They reached ImGui before and the
			// framework did nothing with them, so nothing loses a control; and the D-pad keeps
			// moving only the highlight, which is the standing rule.
			set(Action::kTabPrev,      KeyKind::kKeyboard, 0xC9, PadKind::kButton,   0x0100);   // Page Up / L1
			set(Action::kTabNext,      KeyKind::kKeyboard, 0xD1, PadKind::kButton,   0x0200);   // Page Down / R1
			// Y opened the context menu as a hard-wired special case until now; it is an ordinary
			// bindable action from 1.9.6. It shares Y with the on-screen keyboard's backspace, which
			// the exclusivity rule allows because the keyboard's actions only act while it is open.
			set(Action::kContextMenu,  KeyKind::kNone,       -1, PadKind::kButton,   0x8000);   // Y
			set(Action::kFavourite,    KeyKind::kKeyboard, 0x21, PadKind::kButton,   0x0040);   // F / L3
			// GRAB AND MOVE (the owner, 2026-10-02: "pressing right stick will select the mod and then going and
			// moving the stick up or down will move its position up or down. And this should be rebindable").
			// R3 here too, as in Skyrim. R3 is the target lock in GAMEPLAY, but the game takes no input while this menu is
			// open (the pad gate), so the two never meet (the owner, 2026-10-02: "Obviously, you're not going to target
			// lock while in the menu").
			set(Action::kGrabMod,      KeyKind::kNone,       -1, PadKind::kButton,   0x0080);   // R3
			set(Action::kGrabUp,       KeyKind::kNone,       -1, PadKind::kStickDir, 0x10);     // right stick up
			set(Action::kGrabDown,     KeyKind::kNone,       -1, PadKind::kStickDir, 0x11);     // right stick down
		}

		// Two functions that can be live at the same moment must not share a binding. Everything
		// here is live together inside the menu, except the on-screen keyboard's three, which only
		// act while the keyboard is up - so they may reuse a navigation binding and nothing else.
		bool ExclusiveTogether(Action a, Action b)
		{
			const auto osk = [](Action x) {
				return x == Action::kOskShift || x == Action::kOskBackspace || x == Action::kOskDone;
			};
			if (osk(a) && osk(b)) { return true; }
			if (osk(a) != osk(b)) { return false; }
			return true;
		}

		bool IsReserved(std::int32_t a_code)
		{
			std::int32_t buffer[32]{};
			const auto count = SMF_GetReservedKeyCodes(buffer, static_cast<std::uint32_t>(std::size(buffer)));
			for (std::uint32_t i = 0; i < count && i < std::size(buffer); ++i)
			{
				if (buffer[i] == a_code) { return true; }
			}
			return false;
		}

		// Is this key reserved only because one of the framework's own actions already has it?
		bool HeldByOurOwnAction(std::int32_t a_code)
		{
			for (const Binding& b : g_bindings)
			{
				if (b.keyKind == KeyKind::kKeyboard && b.keyCode == a_code) { return true; }
			}
			return false;
		}

		// A readable name for a DirectInput scan code. Only the keys a player is likely to bind are
		// named; anything else is shown as its code, which is still enough to tell two apart.
		std::string ScanCodeName(std::uint32_t a_code)
		{
			switch (a_code)
			{
			case 0x01: return "Escape";       case 0x0E: return "Backspace";  case 0x0F: return "Tab";
			case 0x1C: return "Enter";        case 0x39: return "Space";      case 0x3B: return "F1";
			case 0x3C: return "F2";           case 0x3D: return "F3";         case 0x3E: return "F4";
			case 0x3F: return "F5";           case 0x40: return "F6";         case 0x41: return "F7";
			case 0x42: return "F8";           case 0x43: return "F9";         case 0x44: return "F10";
			case 0x57: return "F11";          case 0x58: return "F12";
			case 0xC8: return "Up";           case 0xD0: return "Down";       case 0xCB: return "Left";
			case 0xCD: return "Right";        case 0xC7: return "Home";       case 0xCF: return "End";
			case 0xC9: return "Page Up";      case 0xD1: return "Page Down";  case 0xD3: return "Delete";
			case 0x2A: return "Left Shift";   case 0x36: return "Right Shift";
			case 0x1D: return "Left Ctrl";    case 0x9D: return "Right Ctrl";
			case 0x38: return "Left Alt";     case 0xB8: return "Right Alt";
			default: break;
			}
			// Letters and digits, in scan-code order.
			static const char* kLetters = "QWERTYUIOP";
			if (a_code >= 0x10 && a_code <= 0x19) { return std::string(1, kLetters[a_code - 0x10]); }
			static const char* kHome = "ASDFGHJKL";
			if (a_code >= 0x1E && a_code <= 0x26) { return std::string(1, kHome[a_code - 0x1E]); }
			static const char* kBottom = "ZXCVBNM";
			if (a_code >= 0x2C && a_code <= 0x32) { return std::string(1, kBottom[a_code - 0x2C]); }
			if (a_code >= 0x02 && a_code <= 0x0A) { return std::string(1, static_cast<char>('1' + (a_code - 0x02))); }
			if (a_code == 0x0B) { return "0"; }
			char buf[24];
			std::snprintf(buf, sizeof(buf), "key 0x%02X", a_code);
			return buf;
		}

		const char* StickDirName(std::int32_t a_code)
		{
			const int stick = (a_code >> 4) & 0xF;
			const int dir = a_code & 0xF;
			static const char* kLeft[] = { "AMF_BindLStickUp", "AMF_BindLStickDown", "AMF_BindLStickLeft", "AMF_BindLStickRight" };
			static const char* kRight[] = { "AMF_BindRStickUp", "AMF_BindRStickDown", "AMF_BindRStickLeft", "AMF_BindRStickRight" };
			static const char* kLeftEn[] = { "Left stick up", "Left stick down", "Left stick left", "Left stick right" };
			static const char* kRightEn[] = { "Right stick up", "Right stick down", "Right stick left", "Right stick right" };
			if (dir < 0 || dir > 3) { return TR("AMF_BindUnbound", "unbound"); }
			return stick == 0 ? TR(kLeft[dir], kLeftEn[dir]) : TR(kRight[dir], kRightEn[dir]);
		}

		const char* PadButtonName(std::int32_t a_mask)
		{
			switch (a_mask)
			{
			case 0x0001: return TR("AMF_BindDpadUp", "D-pad up");
			case 0x0002: return TR("AMF_BindDpadDown", "D-pad down");
			case 0x0004: return TR("AMF_BindDpadLeft", "D-pad left");
			case 0x0008: return TR("AMF_BindDpadRight", "D-pad right");
			case 0x0010: return TR("AMF_BindStart", "Start");
			case 0x0020: return TR("AMF_BindBack", "Back");
			case 0x0040: return TR("AMF_BindL3", "L3 (left stick click)");
			case 0x0080: return TR("AMF_BindR3", "R3 (right stick click)");
			case 0x0100: return TR("AMF_BindL1", "L1");
			case 0x0200: return TR("AMF_BindR1", "R1");
			case 0x1000: return TR("AMF_BindA", "A");
			case 0x2000: return TR("AMF_BindB", "B");
			case 0x4000: return TR("AMF_BindX", "X");
			case 0x8000: return TR("AMF_BindY", "Y");
			default:     return TR("AMF_BindUnbound", "unbound");
			}
		}
	}

	const char* Label(Action a_action)
	{
		switch (a_action)
		{
		case Action::kToggleMenu:   return TR("AMF_ActToggleMenu", "Open and close the menu");
		case Action::kClose:        return TR("AMF_ActClose", "Close the menu");
		case Action::kUp:           return TR("AMF_ActUp", "Move up");
		case Action::kDown:         return TR("AMF_ActDown", "Move down");
		case Action::kLeft:         return TR("AMF_ActLeft", "Move left");
		case Action::kRight:        return TR("AMF_ActRight", "Move right");
		case Action::kActivate:     return TR("AMF_ActActivate", "Activate");
		case Action::kBack:         return TR("AMF_ActBack", "Back");
		case Action::kPaneLeft:     return TR("AMF_ActPaneLeft", "Back to the mod list");
		case Action::kPaneRight:    return TR("AMF_ActPaneRight", "Into the page");
		case Action::kOskShift:     return TR("AMF_ActOskShift", "On-screen keyboard: shift");
		case Action::kOskBackspace: return TR("AMF_ActOskBackspace", "On-screen keyboard: backspace");
		case Action::kOskDone:      return TR("AMF_ActOskDone", "On-screen keyboard: done");
		case Action::kTabPrev:      return TR("AMF_ActTabPrev", "Previous tab");
		case Action::kTabNext:      return TR("AMF_ActTabNext", "Next tab");
		case Action::kContextMenu:  return TR("AMF_ActContextMenu", "Open a mod's options");
		case Action::kFavourite:    return TR("AMF_ActFavourite", "Favourite the highlighted mod");
		case Action::kGrabMod:      return TR("AMF_ActGrabMod", "Pick up a mod to move it");
		case Action::kGrabUp:       return TR("AMF_ActGrabUp", "Move the picked-up mod up");
		case Action::kGrabDown:     return TR("AMF_ActGrabDown", "Move the picked-up mod down");
		default:                    return "";
		}
	}

	const char* Description(Action a_action)
	{
		switch (a_action)
		{
		case Action::kToggleMenu:   return TR("AMF_ActToggleMenuHelp", "The one key that both opens and closes the menu. The journal's SKSE MENUS row opens it too.");
		case Action::kClose:        return TR("AMF_ActCloseHelp", "Closes the menu without opening it, so the game keeps this control the rest of the time.");
		case Action::kPaneLeft:     return TR("AMF_ActPaneLeftHelp", "From a mod's page, back to the list of mods. On the first section only.");
		case Action::kPaneRight:    return TR("AMF_ActPaneRightHelp", "From the list of mods, into the page beside it.");
		case Action::kTabNext:      return TR("AMF_ActTabNextHelp", "Walks the tabs across the top of a page. The D-pad never changes a tab - move the highlight onto one and activate it, or use these.");
		case Action::kContextMenu:  return TR("AMF_ActContextMenuHelp", "The same menu a right-click opens: favourite, rename, move to the top.");
		case Action::kFavourite:    return TR("AMF_ActFavouriteHelp", "Pins or unpins the highlighted mod without opening the menu first.");
		case Action::kGrabMod:      return TR("AMF_ActGrabModHelp", "Picks up the highlighted mod in the list. Move it with the two controls below, one place at a time; press this again, or B, to put it down.");
		case Action::kGrabUp:
		case Action::kGrabDown:     return TR("AMF_ActGrabMoveHelp", "Moves a picked-up mod one place. Held on a stick, it keeps stepping.");
		default:                    return "";
		}
	}

	namespace
	{
		std::array<bool, static_cast<std::size_t>(Action::kCount)> g_fired{};
	}

	void RaiseTriggered(Action a_action)
	{
		std::scoped_lock lock(g_lock);
		const auto i = static_cast<std::size_t>(a_action);
		if (i < g_fired.size()) { g_fired[i] = true; }
	}

	void RaiseAllFor(std::uint32_t a_code, bool a_gamepad)
	{
		// Only the actions that ARE commands: the navigation ones become ImGui keys instead and
		// must not be raised as well, or a press would both move the cursor and fire a command.
		static constexpr Action kCommands[] = {
			Action::kTabPrev, Action::kTabNext, Action::kContextMenu, Action::kFavourite,
			Action::kGrabMod, Action::kGrabUp, Action::kGrabDown
		};
		std::scoped_lock lock(g_lock);
		for (Action a : kCommands)
		{
			const Binding& b = g_bindings[static_cast<std::size_t>(a)];
			const bool hit = a_gamepad
				? (b.padKind == PadKind::kButton && b.padCode == static_cast<std::int32_t>(a_code))
				: (b.keyKind == KeyKind::kKeyboard && b.keyCode == static_cast<std::int32_t>(a_code));
			if (hit) { g_fired[static_cast<std::size_t>(a)] = true; }
		}
	}

	bool TakeTriggered(Action a_action)
	{
		std::scoped_lock lock(g_lock);
		const auto i = static_cast<std::size_t>(a_action);
		if (i >= g_fired.size() || !g_fired[i]) { return false; }
		g_fired[i] = false;
		return true;
	}

	Binding Get(Action a_action)
	{
		std::scoped_lock lock(g_lock);
		const auto i = static_cast<std::size_t>(a_action);
		return i < g_bindings.size() ? g_bindings[i] : Binding{};
	}

	void ResetToDefaults()
	{
		std::scoped_lock lock(g_lock);
		SetDefaultsLocked();
		logger::info("bindings: reset to the shipped defaults");
	}

	void Unbind(Action a_action, bool a_gamepadSide)
	{
		std::scoped_lock lock(g_lock);
		Binding& b = g_bindings[static_cast<std::size_t>(a_action)];
		if (a_gamepadSide) { b.padKind = PadKind::kNone; b.padCode = -1; }
		else               { b.keyKind = KeyKind::kNone; b.keyCode = -1; }
		g_refusal.clear();
		logger::info("bindings: \"{}\" unbound on the {} side", Label(a_action), a_gamepadSide ? "controller" : "keyboard");
	}

	std::string KeyText(Action a_action)
	{
		const Binding b = Get(a_action);
		if (b.keyKind == KeyKind::kMouse)
		{
			return std::string(TR("AMF_BindMouse", "Mouse")) + " " + std::to_string(b.keyCode + 1);
		}
		if (b.keyKind != KeyKind::kKeyboard || b.keyCode < 0) { return TR("AMF_BindUnbound", "unbound"); }
		return ScanCodeName(static_cast<std::uint32_t>(b.keyCode));
	}

	std::string PadText(Action a_action)
	{
		const Binding b = Get(a_action);
		if (b.padKind == PadKind::kButton) { return PadButtonName(b.padCode); }
		if (b.padKind == PadKind::kStickDir) { return StickDirName(b.padCode); }
		return TR("AMF_BindUnbound", "unbound");
	}

	void BeginCapture(Action a_action, bool a_gamepadSide)
	{
		std::scoped_lock lock(g_lock);
		logger::info("bindings: capture armed for \"{}\" on the {} side", Label(a_action),
					 a_gamepadSide ? "controller" : "keyboard");
		g_capturing = true;
		g_captureAction = a_action;
		g_captureGamepad = a_gamepadSide;
		g_refusal.clear();
	}

	void CancelCapture()
	{
		std::scoped_lock lock(g_lock);
		g_capturing = false;
		g_captureAction = Action::kCount;
	}

	bool IsCapturing()          { std::scoped_lock lock(g_lock); return g_capturing; }
	Action CapturingAction()    { std::scoped_lock lock(g_lock); return g_captureAction; }
	bool CapturingGamepadSide() { std::scoped_lock lock(g_lock); return g_captureGamepad; }
	const char* LastRefusal()   { std::scoped_lock lock(g_lock); return g_refusal.c_str(); }

	namespace
	{
		// Shared by every Offer*: refuse a binding that another simultaneously-live function
		// already holds, and say which one.
		bool TakeLocked(Action a_action, bool a_gamepadSide, KeyKind a_kk, std::int32_t a_kc,
						PadKind a_pk, std::int32_t a_pc)
		{
			for (std::size_t i = 0; i < g_bindings.size(); ++i)
			{
				const auto other = static_cast<Action>(i);
				if (other == a_action) { continue; }
				if (!ExclusiveTogether(a_action, other)) { continue; }
				const Binding& b = g_bindings[i];
				const bool clash = a_gamepadSide
					? (b.padKind == a_pk && b.padCode == a_pc && a_pk != PadKind::kNone)
					: (b.keyKind == a_kk && b.keyCode == a_kc && a_kk != KeyKind::kNone);
				if (clash)
				{
					g_refusal = std::string(TR("AMF_BindTaken", "Already used by")) + " \"" + Label(other) + "\"";
					logger::info("bindings: refused - that control already belongs to \"{}\"", Label(other));
					return false;
				}
			}
			Binding& mine = g_bindings[static_cast<std::size_t>(a_action)];
			if (a_gamepadSide) { mine.padKind = a_pk; mine.padCode = a_pc; }
			else               { mine.keyKind = a_kk; mine.keyCode = a_kc; }
			g_capturing = false;
			g_captureAction = Action::kCount;
			g_refusal.clear();
			// Logged like the refusals are: a capture that silently succeeded and a capture that
			// silently never fired read the same in a log, which is how the F1 case above went
			// unexplained for a whole test round.
			// The names are formed from the raw codes here: KeyText/PadText take g_lock, which this
			// function already holds, and std::mutex is not recursive - calling them would deadlock
			// the input thread on the first successful bind.
			logger::info("bindings: \"{}\" bound on the {} side ({})", Label(a_action),
						 a_gamepadSide ? "controller" : "keyboard",
						 a_gamepadSide
							 ? (a_pk == PadKind::kStickDir ? StickDirName(a_pc) : PadButtonName(a_pc))
							 : (a_kk == KeyKind::kMouse
									? (std::string("mouse ") + std::to_string(a_kc + 1))
									: ScanCodeName(static_cast<std::uint32_t>(a_kc))));
			return true;
		}
	}

	bool OfferKeyboard(std::uint32_t a_scancode, bool a_down)
	{
		if (!a_down) { return false; }
		std::scoped_lock lock(g_lock);
		if (!g_capturing || g_captureGamepad) { return false; }
		if (a_scancode == kDIKEscape) { g_capturing = false; g_captureAction = Action::kCount; return true; }
		// THE RESERVED LIST IS NOT CONSULTED HERE (2026-09-19, second correction). It exists to tell
		// OTHER mods which keys this framework has taken, and it is composed with the LIVE menu key
		// first - so every key on it is one of THESE actions' own. Checking it from this page was
		// circular: it refused F1 for "open and close the menu" because F1 was the menu key, and the
		// first fix (exempt a key one of our actions still holds) only papered over it - unbind F1
		// and the exemption stopped applying, so it was refused again, silently, exactly as the owner
		// found: "AMF did let me rebind the opening close key back to F1, but not after I unbound it
		// entirely." The clash check below is the real guard, and it is the right one: it stops two
		// simultaneously-live functions sharing a control, which is what the rule is actually for.
		TakeLocked(g_captureAction, false, KeyKind::kKeyboard, static_cast<std::int32_t>(a_scancode), PadKind::kNone, -1);
		return true;
	}

	bool OfferMouse(std::uint32_t a_button, bool a_down)
	{
		if (!a_down) { return false; }
		std::scoped_lock lock(g_lock);
		if (!g_capturing || g_captureGamepad) { return false; }
		TakeLocked(g_captureAction, false, KeyKind::kMouse, static_cast<std::int32_t>(a_button), PadKind::kNone, -1);
		return true;
	}

	bool OfferGamepad(std::uint32_t a_mask, bool a_down)
	{
		if (!a_down) { return false; }
		std::scoped_lock lock(g_lock);
		if (!g_capturing || !g_captureGamepad)
		{
			// Logged because "the D-pad navigated instead of binding" (the owner, 2026-09-19) has two
			// completely different causes - no capture armed, or a capture armed on the OTHER tab -
			// and they are indistinguishable on screen.
			logger::debug("bindings: pad 0x{:04X} offered but not taken (capturing={}, gamepadSide={})",
						  a_mask, g_capturing, g_captureGamepad);
			return false;
		}
		if (a_mask == kPadB) { g_capturing = false; g_captureAction = Action::kCount; return true; }
		TakeLocked(g_captureAction, true, KeyKind::kNone, -1, PadKind::kButton, static_cast<std::int32_t>(a_mask));
		return true;
	}

	bool OfferStick(int a_which, float a_x, float a_y)
	{
		std::scoped_lock lock(g_lock);
		if (!g_capturing || !g_captureGamepad) { return false; }
		// Well past the navigation deadzone, so a resting stick can never become a binding.
		constexpr float kBindThreshold = 0.75f;
		int dir = -1;
		if (a_y > kBindThreshold)       { dir = 0; }
		else if (a_y < -kBindThreshold) { dir = 1; }
		else if (a_x < -kBindThreshold) { dir = 2; }
		else if (a_x > kBindThreshold)  { dir = 3; }
		if (dir < 0) { return false; }
		const std::int32_t code = static_cast<std::int32_t>(((a_which == 1 ? 1 : 0) << 4) | dir);
		TakeLocked(g_captureAction, true, KeyKind::kNone, -1, PadKind::kStickDir, code);
		return true;
	}

	namespace
	{
		std::mutex g_consumerLock;
		bool       g_consumerArmed = false;
		bool       g_consumerGamepad = false;
		ConsumerCaptureState g_consumerState = ConsumerCaptureState::kIdle;
		std::int32_t g_consumerKind = -1, g_consumerCode = -1;
		std::chrono::steady_clock::time_point g_consumerDeadline{};

		void ExpireLocked()
		{
			if (g_consumerArmed && std::chrono::steady_clock::now() > g_consumerDeadline) {
				g_consumerArmed = false;
				g_consumerState = ConsumerCaptureState::kTimedOut;
				logger::info("bindings: a mod's key capture timed out");
			}
		}
	}

	void BeginConsumerCapture(bool a_gamepadSide, std::int32_t a_timeoutMs)
	{
		std::scoped_lock lock(g_consumerLock);
		g_consumerArmed = true;
		g_consumerGamepad = a_gamepadSide;
		g_consumerState = ConsumerCaptureState::kWaiting;
		g_consumerKind = g_consumerCode = -1;
		g_consumerDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::clamp(a_timeoutMs, 1000, 30000));
		logger::info("bindings: a mod armed a key capture on the {} side ({} ms)", a_gamepadSide ? "controller" : "keyboard", a_timeoutMs);
	}

	void CancelConsumerCapture()
	{
		std::scoped_lock lock(g_consumerLock);
		if (g_consumerArmed) {
			g_consumerState = ConsumerCaptureState::kCancelled;
			logger::info("bindings: a mod's key capture was cancelled (the mod asked, or the window closed)");
		}
		g_consumerArmed = false;
	}

	bool ConsumerCapturing()
	{
		std::scoped_lock lock(g_consumerLock);
		ExpireLocked();
		return g_consumerArmed;
	}

	bool ConsumerCapturingGamepad()
	{
		std::scoped_lock lock(g_consumerLock);
		return g_consumerArmed && g_consumerGamepad;
	}

	ConsumerCaptureState PollConsumerCapture(std::int32_t* a_kind, std::int32_t* a_code)
	{
		std::scoped_lock lock(g_consumerLock);
		ExpireLocked();
		const auto s = g_consumerState;
		if (s == ConsumerCaptureState::kCaptured) {
			if (a_kind) { *a_kind = g_consumerKind; }
			if (a_code) { *a_code = g_consumerCode; }
		}
		if (s != ConsumerCaptureState::kWaiting) { g_consumerState = ConsumerCaptureState::kIdle; }
		return s;
	}

	bool OfferConsumer(std::int32_t a_kind, std::int32_t a_code, bool a_down, bool a_gamepadSide)
	{
		std::scoped_lock lock(g_consumerLock);
		ExpireLocked();
		if (!g_consumerArmed || g_consumerGamepad != a_gamepadSide) { return false; }
		// Presses on the capturing side are taken; a release passes (the A that armed the capture must reach ImGui).
		if (!a_down) { return false; }
		if (!a_gamepadSide && a_kind == 0 && a_code == static_cast<std::int32_t>(kDIKEscape)) {
			g_consumerArmed = false;
			g_consumerState = ConsumerCaptureState::kCancelled;
			logger::info("bindings: a mod's key capture was cancelled with Escape");
			return true;
		}
		g_consumerArmed = false;
		g_consumerState = ConsumerCaptureState::kCaptured;
		g_consumerKind = a_kind;
		g_consumerCode = a_code;
		logger::info("bindings: a mod's key capture took kind {} code {}", a_kind, a_code);
		return true;
	}

	Action FromKeyboard(std::uint32_t a_scancode)
	{
		std::scoped_lock lock(g_lock);
		for (std::size_t i = 0; i < g_bindings.size(); ++i)
		{
			if (g_bindings[i].keyKind == KeyKind::kKeyboard &&
				g_bindings[i].keyCode == static_cast<std::int32_t>(a_scancode))
			{
				return static_cast<Action>(i);
			}
		}
		return Action::kCount;
	}

	Action FromMouse(std::uint32_t a_button)
	{
		std::scoped_lock lock(g_lock);
		for (std::size_t i = 0; i < g_bindings.size(); ++i)
		{
			if (g_bindings[i].keyKind == KeyKind::kMouse &&
				g_bindings[i].keyCode == static_cast<std::int32_t>(a_button))
			{
				return static_cast<Action>(i);
			}
		}
		return Action::kCount;
	}

	Action FromGamepad(std::uint32_t a_mask)
	{
		std::scoped_lock lock(g_lock);
		for (std::size_t i = 0; i < g_bindings.size(); ++i)
		{
			if (g_bindings[i].padKind == PadKind::kButton &&
				g_bindings[i].padCode == static_cast<std::int32_t>(a_mask))
			{
				return static_cast<Action>(i);
			}
		}
		return Action::kCount;
	}

	Action FromStick(int a_which, int a_direction)
	{
		const std::int32_t code = static_cast<std::int32_t>(((a_which == 1 ? 1 : 0) << 4) | (a_direction & 0xF));
		std::scoped_lock lock(g_lock);
		for (std::size_t i = 0; i < g_bindings.size(); ++i)
		{
			if (g_bindings[i].padKind == PadKind::kStickDir && g_bindings[i].padCode == code)
			{
				return static_cast<Action>(i);
			}
		}
		return Action::kCount;
	}

	namespace
	{
		const char* kKeys[] = {
			"ToggleMenu", "Close", "Up", "Down", "Left", "Right", "Activate", "Back",
			"PaneLeft", "PaneRight", "OskShift", "OskBackspace", "OskDone",
			"TabPrev", "TabNext", "ContextMenu", "Favourite", "GrabMod", "GrabUp", "GrabDown"
		};
		static_assert(sizeof(kKeys) / sizeof(kKeys[0]) == static_cast<std::size_t>(Action::kCount));
	}

	void LoadFrom(const std::unordered_map<std::string, std::string>& a_iniEntries)
	{
		std::scoped_lock lock(g_lock);
		SetDefaultsLocked();
		std::size_t read = 0;
		for (std::size_t i = 0; i < g_bindings.size(); ++i)
		{
			// One line per action: "<keyKind>,<keyCode>,<padKind>,<padCode>". A line that is
			// absent or malformed leaves the shipped default alone rather than unbinding it.
			const auto it = a_iniEntries.find(std::string("Bindings.s") + kKeys[i]);
			if (it == a_iniEntries.end() || it->second.empty()) { continue; }
			std::stringstream stream(it->second);
			std::string part;
			std::array<int, 4> v{ 0, -1, 0, -1 };
			std::size_t n = 0;
			while (n < 4 && std::getline(stream, part, ',')) { v[n++] = std::atoi(part.c_str()); }
			if (n != 4) { continue; }
			g_bindings[i] = Binding{ static_cast<KeyKind>(v[0]), v[1], static_cast<PadKind>(v[2]), v[3] };
			++read;
		}
		logger::info("bindings: {} of {} read from the INI; the rest are the shipped defaults",
					 read, g_bindings.size());
	}

	std::string IniBlock()
	{
		std::scoped_lock lock(g_lock);
		std::string text =
			"\n[Bindings]\n"
			"; The framework's own controls, one line per function:\n"
			";   s<Function>=<keyKind>,<keyCode>,<padKind>,<padCode>\n"
			"; keyKind 0 = none, 1 = keyboard (DirectInput scan code), 2 = mouse button (0 = left).\n"
			"; padKind 0 = none, 1 = pad button (XInput mask; L3 = 64, R3 = 128), 2 = stick direction\n"
			";         (code = stick * 16 + direction; stick 0 = left, 1 = right; direction 0 = up,\n"
			";          1 = down, 2 = left, 3 = right).\n"
			"; Set these from Controls in the menu - its two tabs capture the key or button you press.\n";
		for (std::size_t i = 0; i < g_bindings.size(); ++i)
		{
			const Binding& b = g_bindings[i];
			text += std::string("s") + kKeys[i] + "=" +
					std::to_string(static_cast<int>(b.keyKind)) + "," + std::to_string(b.keyCode) + "," +
					std::to_string(static_cast<int>(b.padKind)) + "," + std::to_string(b.padCode) + "\n";
		}
		return text;
	}
}
