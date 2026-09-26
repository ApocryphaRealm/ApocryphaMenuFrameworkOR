#include "Input.h"
#include "Keyboard.h"

#include <chrono>
#include <cmath>

#include "Bindings.h"
#include "Compat.h"
#include "Renderer.h"
#include "Settings.h"
#include "Logger.h"

#include <imgui.h>
#include <Xinput.h>
#include <imgui_internal.h>   // GImGui: the active item is asked for directly, so a text field can keep the D-pad

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace input
{
	namespace
	{
		// -----------------------------------------------------------------------------------
		// The record the input thread hands to the render thread. Everything ImGui needs,
		// nothing that dereferences game memory later - the InputEvent list is dead the moment
		// the hook returns, so records copy by value.
		// -----------------------------------------------------------------------------------
		struct Record
		{
			enum class Kind : std::uint8_t
			{
				kMouseMove,
				kMouseButton,
				kMouseWheel,
				kKeyboard,
				kGamepad,
				kThumbstick,  // left stick, for menu nav in controller mode (x,y in [-1,1])
				kCharacter,
				kCursorSet,   // absolute placement from a driver (DevBench)
				kMouseAbs,    // the OS cursor's client position (WM_MOUSEMOVE), Oblivion Remastered
			};

			Kind kind{};
			std::uint32_t code = 0;   // idCode: mouse button index / DIK scancode / XInput mask / unicode
			bool down = false;        // press (true) or release (false) transitions only
			float x = 0.0f;           // mouse deltas / wheel direction
			float y = 0.0f;
		};

		std::mutex g_queueLock;
		std::vector<Record> g_queue;

		// Armed by BeginRebindToggleKey(); the next keyboard press in the hook becomes the toggle
		// key (Escape cancels). Atomic - set on the render thread, consumed on the input thread.
		std::atomic<bool> g_awaitingRebind{ false };

		// Observe-only keybind capture (amf.keybind, L26): armed over DevBench, records the next
		// keyboard/gamepad press without consuming it, then disarms. -1 = nothing captured yet.
		// AUTO INPUT MODE (the author, 2026-09-01). What the player last really used, and when.
		// Only DELIBERATE input counts: a key or gamepad button going down, a mouse button, real
		// mouse movement, or a stick pushed past the navigation deadzone. Idle noise - a resting
		// stick, a nudged mouse - must never flip the mode, which is the whole reason the framework
		// refused to auto-detect before this was asked for.
		std::atomic<Device> g_lastDevice{ Device::kUnknown };
		std::atomic<std::chrono::steady_clock::time_point> g_lastDeviceAt{ std::chrono::steady_clock::time_point{} };
		constexpr float kMouseMoveThreshold = 2.0f;   // pixels in one event
		constexpr float kStickThreshold = 0.35f;      // same as the nav deadzone

		void NoteDevice(Device a_device)
		{
			const Device was = g_lastDevice.exchange(a_device, std::memory_order_relaxed);
			g_lastDeviceAt.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
			if (was == a_device) { return; }

			const bool wantsController = (a_device == Device::kGamepad);
			logger::info("input: {} used -> {} navigation",
						 wantsController ? "controller" : "keyboard/mouse",
						 wantsController ? "controller" : "keyboard");
		}

		// The sticks as they really are, kept so a consumer can read them apart (1.9.5). The
		// translation below collapses them onto one set of nav axes; these are the raw values.
		std::atomic<float> g_stickX[2]{};
		std::atomic<float> g_stickY[2]{};
		std::atomic<bool>  g_stickClicked[2]{};
		std::atomic<bool>  g_sticksCaptured{ false };

		// B pressed while a text field held the keyboard. The key itself is kept from ImGui (it
		// would revert the text), so the renderer cannot ask ImGui whether it happened - it asks
		// here instead, and the flag is consumed on read.
		std::atomic<bool>  g_textFieldCancel{ false };

		// Set by the renderer each frame: an item is being edited, so the right stick drives it.
		std::atomic<bool> g_itemActive{ false };

		std::atomic<bool> g_captureArmed{ false };
		std::atomic<std::int64_t> g_lastCaptured{ -1 };

		// XInput Start button mask (RE::BSWin32GamepadDevice::kStart) - closes the menu in
		// controller mode, since there is otherwise no gamepad way out (design decision, 2026-08-28).
		constexpr std::uint32_t kGamepadStart = 0x0010;
		constexpr std::uint32_t kDIKEscape = 0x01;

		// Buttons the GAME currently believes are held - maintained on the input thread only.
		// While the menu is open, a release passes through ONLY if its press reached the game
		// before the menu opened. Passing every release (the 1.1.2 behavior) let release-
		// triggered actions fire: Skyrim's shout activates on button RELEASE, so a shout
		// button pressed INSIDE the menu was consumed on the down-edge but completed as a
		// shout on the up-edge (the author's report). Keyed device<<32|idCode.
		std::unordered_set<std::uint64_t> g_gameHeldButtons;

		// Software cursor, owned by the render thread. The game recentres/hides the OS cursor
		// at will, so the only trustworthy position is one we integrate ourselves from the
		// MouseMoveEvent deltas (the Wheeler-lineage approach from the survey).
		float g_cursorX = 0.0f;
		float g_cursorY = 0.0f;
		// Records that must land on a LATER frame (see QueueMouseClick). Drained by the render
		// thread at the top of ProcessQueuedEvents; guarded by the same lock as the main queue.
		struct Deferred { int framesLeft; Record record; };
		std::vector<Deferred> g_deferred;

		std::atomic<float> g_cursorMirrorX{ 0.0f };   // read by DevBench off-thread
		std::atomic<float> g_cursorMirrorY{ 0.0f };

		void Enqueue(const Record& a_record)
		{
			std::scoped_lock lock(g_queueLock);

			// A runaway queue means the render thread stopped draining (e.g. device lost);
			// dropping input is strictly better than growing unbounded on the input thread.
			if (g_queue.size() < 512)
			{
				g_queue.push_back(a_record);
			}
		}

		// -----------------------------------------------------------------------------------
		// DIK scancode -> ImGuiKey. The navigation-and-editing set; full text input is M4.
		// -----------------------------------------------------------------------------------
		// An action, as the ImGui key that performs it. This is the join between the Controls page's
		// bindings and what ImGui's navigation actually reads: whatever the player bound "Move up"
		// to arrives at ImGui as UpArrow/DpadUp, so nav needs no notion of bindings at all.
		// True when an ImGui text field currently holds the keyboard AND this key would move nav.
		// Those keys are dropped rather than fed, so typing survives them; everything else still
		// reaches ImGui, so A, B and the shoulder buttons behave exactly as before.
		bool TextFieldHasTheKeyboard(ImGuiKey a_key)
		{
			switch (a_key)
			{
			case ImGuiKey_GamepadDpadUp:
			case ImGuiKey_GamepadDpadDown:
			case ImGuiKey_GamepadDpadLeft:
			case ImGuiKey_GamepadDpadRight:
			// B is filtered too, and for a different reason. ImGui treats the gamepad cancel as an
			// InputText CANCEL, which RESTORES THE TEXT THE FIELD HAD WHEN EDITING BEGAN - so
			// pressing circle to leave the box threw away what had just been typed and put the
			// previous search back (the owner, 2026-09-19: "it reverted the word that I'd made to the
			// previous word that I'd searched ... It shouldn't revert the word"). Keeping it away
			// from ImGui and ending the edit ourselves (Renderer's B handler calls ClearActiveID)
			// leaves the buffer exactly as typed, which is what "leave the box" should mean.
			case ImGuiKey_GamepadFaceRight:
				break;
			default:
				return false;
			}
			return GImGui && GImGui->ActiveId != 0 && keyboard::IsTextField(GImGui->ActiveId);
		}

		ImGuiKey ActionToImGuiKey(bindings::Action a_action, bool a_gamepad)
		{
			switch (a_action)
			{
			case bindings::Action::kUp:       return a_gamepad ? ImGuiKey_GamepadDpadUp : ImGuiKey_UpArrow;
			case bindings::Action::kDown:     return a_gamepad ? ImGuiKey_GamepadDpadDown : ImGuiKey_DownArrow;
			case bindings::Action::kLeft:     return a_gamepad ? ImGuiKey_GamepadDpadLeft : ImGuiKey_LeftArrow;
			case bindings::Action::kRight:    return a_gamepad ? ImGuiKey_GamepadDpadRight : ImGuiKey_RightArrow;
			case bindings::Action::kActivate: return a_gamepad ? ImGuiKey_GamepadFaceDown : ImGuiKey_Enter;
			case bindings::Action::kBack:     return a_gamepad ? ImGuiKey_GamepadFaceRight : ImGuiKey_Backspace;
			case bindings::Action::kClose:    return a_gamepad ? ImGuiKey_None : ImGuiKey_Escape;
			default:                          return ImGuiKey_None;
			}
		}

		ImGuiKey ScancodeToImGuiKey(std::uint32_t a_scancode)
		{
			// A REBOUND key wins (1.9.6). The table below stays as the fallback, so every key that
			// was not given a new job keeps the one it always had - a player who rebinds nothing
			// notices no difference.
			if (const auto act = bindings::FromKeyboard(a_scancode); act != bindings::Action::kCount)
			{
				if (const ImGuiKey mapped = ActionToImGuiKey(act, false); mapped != ImGuiKey_None) { return mapped; }
			}
			switch (a_scancode)
			{
			case 0x01: return ImGuiKey_Escape;
			case 0x0F: return ImGuiKey_Tab;
			case 0x1C: return ImGuiKey_Enter;
			case 0x39: return ImGuiKey_Space;
			case 0x0E: return ImGuiKey_Backspace;
			case 0xC8: return ImGuiKey_UpArrow;
			case 0xD0: return ImGuiKey_DownArrow;
			case 0xCB: return ImGuiKey_LeftArrow;
			case 0xCD: return ImGuiKey_RightArrow;
			case 0xC7: return ImGuiKey_Home;
			case 0xCF: return ImGuiKey_End;
			case 0xC9: return ImGuiKey_PageUp;
			case 0xD1: return ImGuiKey_PageDown;
			case 0x2A: return ImGuiKey_LeftShift;
			case 0x36: return ImGuiKey_RightShift;
			case 0x1D: return ImGuiKey_LeftCtrl;
			case 0x9D: return ImGuiKey_RightCtrl;
			case 0x38: return ImGuiKey_LeftAlt;
			case 0xB8: return ImGuiKey_RightAlt;
			case 0xD3: return ImGuiKey_Delete;
			// Letters and digits. A typed character still arrives as a CharEvent - these are the
			// KEY events, which is what ImGui's text field needs for the editing shortcuts
			// (Ctrl+A select all, Ctrl+C/X/V, Ctrl+Z) and what a mod reading ImGui::IsKeyPressed
			// needs to see. Without them Ctrl+A in the search bar did nothing.
			case 0x1E: return ImGuiKey_A;  case 0x30: return ImGuiKey_B;  case 0x2E: return ImGuiKey_C;
			case 0x20: return ImGuiKey_D;  case 0x12: return ImGuiKey_E;  case 0x21: return ImGuiKey_F;
			case 0x22: return ImGuiKey_G;  case 0x23: return ImGuiKey_H;  case 0x17: return ImGuiKey_I;
			case 0x24: return ImGuiKey_J;  case 0x25: return ImGuiKey_K;  case 0x26: return ImGuiKey_L;
			case 0x32: return ImGuiKey_M;  case 0x31: return ImGuiKey_N;  case 0x18: return ImGuiKey_O;
			case 0x19: return ImGuiKey_P;  case 0x10: return ImGuiKey_Q;  case 0x13: return ImGuiKey_R;
			case 0x1F: return ImGuiKey_S;  case 0x14: return ImGuiKey_T;  case 0x16: return ImGuiKey_U;
			case 0x2F: return ImGuiKey_V;  case 0x11: return ImGuiKey_W;  case 0x2D: return ImGuiKey_X;
			case 0x15: return ImGuiKey_Y;  case 0x2C: return ImGuiKey_Z;
			case 0x02: return ImGuiKey_1;  case 0x03: return ImGuiKey_2;  case 0x04: return ImGuiKey_3;
			case 0x05: return ImGuiKey_4;  case 0x06: return ImGuiKey_5;  case 0x07: return ImGuiKey_6;
			case 0x08: return ImGuiKey_7;  case 0x09: return ImGuiKey_8;  case 0x0A: return ImGuiKey_9;
			case 0x0B: return ImGuiKey_0;
			default:   return ImGuiKey_None;
			}
		}

		// -----------------------------------------------------------------------------------
		// XInput button mask (RE::BSWin32GamepadDevice::Key) -> ImGuiKey gamepad navigation.
		// Fed only in controller mode (explicit toggle - never auto-detected; nav-drift rule).
		// -----------------------------------------------------------------------------------
		ImGuiKey GamepadMaskToImGuiKey(std::uint32_t a_mask)
		{
			if (const auto act = bindings::FromGamepad(a_mask); act != bindings::Action::kCount)
			{
				if (const ImGuiKey mapped = ActionToImGuiKey(act, true); mapped != ImGuiKey_None) { return mapped; }
			}
			switch (a_mask)
			{
			case 0x0001: return ImGuiKey_GamepadDpadUp;
			case 0x0002: return ImGuiKey_GamepadDpadDown;
			case 0x0004: return ImGuiKey_GamepadDpadLeft;
			case 0x0008: return ImGuiKey_GamepadDpadRight;
			case 0x1000: return ImGuiKey_GamepadFaceDown;   // A = activate
			case 0x2000: return ImGuiKey_GamepadFaceRight;  // B = cancel
			case 0x4000: return ImGuiKey_GamepadFaceLeft;
			case 0x8000: return ImGuiKey_GamepadFaceUp;
			case 0x0100: return ImGuiKey_GamepadL1;
			case 0x0200: return ImGuiKey_GamepadR1;
			// The stick clicks. They were not mapped at all, so a page could not be given an
			// action on one - which is what "press R3 on the list item" needs (1.9.5).
			case 0x0040: return ImGuiKey_GamepadL3;
			case 0x0080: return ImGuiKey_GamepadR3;
			default:     return ImGuiKey_None;
			}
		}

		// -----------------------------------------------------------------------------------
		// THE DECISION (Oblivion Remastered). Skyrim ran it once per engine InputEvent inside a
		// hook on PollInputDevices; Oblivion Remastered has no such stream to hook - Unreal reads
		// keys and the mouse from the window's messages and the pad from XInput - so the same
		// decision runs once per button EDGE from whichever source saw it (the window procedure,
		// the per-frame XInput read, or a driver's injected press). In order:
		//   1. the Controls page's capture, the menu-key rebind and the observe-only witness
		//   2. the menu key -> flip the menu; Start closes it in controller mode
		//   3. menu open -> the edge becomes a record for ImGui and the game does not see it,
		//      except a RELEASE whose press the game saw before the menu opened (no stuck keys)
		//   4. menu closed -> the game sees everything
		// Returns true when the game must NOT see this edge.
		// -----------------------------------------------------------------------------------
		enum class Dev : std::uint32_t { kKeyboard = 0, kMouse = 1, kGamepad = 2 };   // Skyrim's INPUT_DEVICE numbering, kept for LastCapturedKey
		std::mutex g_heldLock;   // g_gameHeldButtons: the window thread and the render thread both decide edges

		std::uint64_t GameKey(Dev a_dev, std::uint32_t a_code) { return (static_cast<std::uint64_t>(a_dev) << 32) | a_code; }

		bool DecideButton(Dev a_dev, std::uint32_t a_code, bool a_down)
		{
			const bool menuOpen = renderer::IsMainWindowVisible();
			const bool controllerMode = UsingController();
			bool passThrough = true;

			if (a_down && a_dev != Dev::kMouse && g_captureArmed.load(std::memory_order_acquire))
			{
				g_lastCaptured.store((static_cast<std::int64_t>(a_dev) << 32) | static_cast<std::int64_t>(a_code), std::memory_order_release);
				g_captureArmed.store(false, std::memory_order_release);
				logger::info("keybind capture: observed device {} code {}", static_cast<std::uint32_t>(a_dev), a_code);
			}

			// THE CONTROLS PAGE'S CAPTURE takes precedence while armed: the press it waits for must not
			// also do whatever it is currently bound to, and the game never sees it either.
			if (bindings::IsCapturing())
			{
				bool taken = false;
				if (a_dev == Dev::kKeyboard)     { taken = bindings::OfferKeyboard(a_code, a_down); }
				else if (a_dev == Dev::kMouse)   { taken = bindings::OfferMouse(a_code, a_down); }
				else                             { taken = bindings::OfferGamepad(a_code, a_down); }
				if (taken)
				{
					settings::Save();
					return true;
				}
			}

			if (g_awaitingRebind.load(std::memory_order_acquire) && a_dev == Dev::kKeyboard && a_down)
			{
				if (a_code != kDIKEscape)
				{
					settings::Get().toggleKey = static_cast<std::int32_t>(a_code);
					settings::Save();
					logger::info("menu toggle key rebound to scan code {}", a_code);
				}
				else
				{
					logger::info("menu toggle key rebind cancelled (Escape)");
				}
				g_awaitingRebind.store(false, std::memory_order_release);
				passThrough = false;
			}
			else if (a_dev == Dev::kKeyboard && a_down && bindings::FromKeyboard(a_code) == bindings::Action::kToggleMenu &&
					 compat::IsHotkeyEnabled())
			{
				renderer::ToggleMainWindow();
				passThrough = false;   // the game never sees the framework's own key
			}
			else if (menuOpen && controllerMode && a_dev == Dev::kGamepad && a_down &&
					 bindings::FromGamepad(a_code) == bindings::Action::kClose)
			{
				renderer::ToggleMainWindow();   // Start closes (never opens) - the controller's way out
				passThrough = false;
			}
			else if (menuOpen)
			{
				switch (a_dev)
				{
				case Dev::kKeyboard: Enqueue({ Record::Kind::kKeyboard, a_code, a_down, 0.0f, 0.0f }); break;
				case Dev::kMouse:    Enqueue({ Record::Kind::kMouseButton, a_code, a_down, 0.0f, 0.0f }); break;
				case Dev::kGamepad:  Enqueue({ Record::Kind::kGamepad, a_code, a_down, 0.0f, 0.0f }); break;
				}
				passThrough = false;
				if (!a_down)
				{
					// Pass the release ONLY if the game saw the press (held across the open transition).
					std::scoped_lock l(g_heldLock);
					if (g_gameHeldButtons.erase(GameKey(a_dev, a_code)) > 0) { passThrough = true; }
				}
			}

			if (passThrough)
			{
				std::scoped_lock l(g_heldLock);
				if (a_down) { g_gameHeldButtons.insert(GameKey(a_dev, a_code)); }
				else        { g_gameHeldButtons.erase(GameKey(a_dev, a_code)); }
			}
			return !passThrough;
		}

		// DirectInput scan code from a key message: bits 16-23 are the scan code and bit 24 the
		// extended flag, which DirectInput writes as 0x80 (E0 48 = Up = 0xC8). A message posted by a
		// driver may carry no extended flag for the navigation keys, so those are forced.
		std::uint32_t ScanFromMessage(WPARAM a_vk, LPARAM a_lp)
		{
			std::uint32_t sc = static_cast<std::uint32_t>((a_lp >> 16) & 0xFF);
			if (sc == 0) { sc = ::MapVirtualKeyW(static_cast<UINT>(a_vk), MAPVK_VK_TO_VSC) & 0xFF; }
			bool extended = (a_lp & (1LL << 24)) != 0;
			switch (a_vk)
			{
			case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT: case VK_HOME: case VK_END:
			case VK_PRIOR: case VK_NEXT: case VK_INSERT: case VK_DELETE: case VK_RCONTROL: case VK_RMENU:
				extended = true;
				break;
			default:
				break;
			}
			return extended ? (sc | 0x80u) : sc;
		}

		// The mouse. Positions come from WM_MOUSEMOVE while the OS cursor is free (PollGamepad releases
		// Unreal's clip every frame the menu is up); raw deltas are the fallback for a cursor something
		// still holds in place, so the pointer moves either way and never twice.
		std::atomic<std::int64_t> g_lastAbsMoveMs{ 0 };
		int g_lastAbsX = -1, g_lastAbsY = -1;
		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		// The pad, read once per frame.
		using XInputGetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
		XInputGetState_t g_xinput = nullptr;
		WORD g_padButtons = 0;
		float g_padSent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // lx ly rx ry last queued

		// Driver presses, walked by PollGamepad on the render thread through DecideButton - the path a
		// real key takes, so a test drives the shipped decision rather than a shortcut around it.
		struct Injected { Dev dev; std::uint32_t code; int framesLeft; bool downSent; };
		std::mutex g_injectLock;
		std::vector<Injected> g_injected;
		std::vector<std::uint32_t> g_injectedChars;
	}

	bool Install()
	{
		// Nothing to hook: the overlay's window procedure calls OnWindowMessage and the renderer calls
		// PollGamepad each frame. XInput is NOT touched here - see ResolveXInput.
		logger::info("input: keyboard and mouse through the game window's messages; menu key 0x{:X}", settings::Get().toggleKey);
		return true;
	}

	namespace
	{
		// XInput is resolved the first time the menu opens, never at plugin load. Loading xinput1_4.dll at OBSE's
		// load - before the game and Steam's overlay had set up their controller path - left the controller dead in
		// the game for the whole session, menu never opened (the owner's bisect, 2026-09-26: OBSE64 alone worked,
		// OBSE64 + AMF did not). The DLL the game itself loaded is preferred, so the framework reads the pad through
		// the same (Steam-hooked) entry point the game does; loading one is the last resort.
		void ResolveXInput()
		{
			static bool s_tried = false;
			if (s_tried) { return; }
			s_tried = true;
			constexpr const wchar_t* kDlls[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
			for (const bool load : { false, true })
			{
				for (const wchar_t* dll : kDlls)
				{
					HMODULE m = load ? ::LoadLibraryW(dll) : ::GetModuleHandleW(dll);   // xinput-deferred: first menu open, never at plugin load
					if (!m) { continue; }
					g_xinput = reinterpret_cast<XInputGetState_t>(::GetProcAddress(m, "XInputGetState"));
					if (g_xinput)
					{
						logger::info("input: controller read through {} ({})", std::filesystem::path(dll).string(),
									 load ? "loaded by the framework - the game had none" : "the game's own copy");
						return;
					}
				}
			}
			logger::warn("input: no XInput DLL found - the menu takes keyboard and mouse only");
		}
	}

	bool OnWindowMessage(HWND, UINT a_msg, WPARAM a_wp, LPARAM a_lp)
	{
		const bool open = renderer::IsMainWindowVisible();
		switch (a_msg)
		{
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYUP:
			{
				const bool down = (a_msg == WM_KEYDOWN || a_msg == WM_SYSKEYDOWN);
				if (down && (a_lp & (1LL << 30)))
				{
					return open;   // auto-repeat: ImGui repeats a held key itself; the game gets none while the menu is up
				}
				return DecideButton(Dev::kKeyboard, ScanFromMessage(a_wp, a_lp), down);
			}
		case WM_CHAR:
		case WM_SYSCHAR:
			if (!open) { return false; }
			if (a_wp >= 0x20 && a_wp != 0x7F) { Enqueue({ Record::Kind::kCharacter, static_cast<std::uint32_t>(a_wp), true, 0.0f, 0.0f }); }
			return true;
		case WM_MOUSEMOVE:
			if (!open) { return false; }
			{
				const int x = static_cast<short>(LOWORD(a_lp)), y = static_cast<short>(HIWORD(a_lp));
				if (x != g_lastAbsX || y != g_lastAbsY)
				{
					g_lastAbsX = x;
					g_lastAbsY = y;
					g_lastAbsMoveMs.store(NowMs(), std::memory_order_relaxed);
					Enqueue({ Record::Kind::kMouseAbs, 0, false, static_cast<float>(x), static_cast<float>(y) });
				}
			}
			return true;
		case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
			return DecideButton(Dev::kMouse, 0, a_msg != WM_LBUTTONUP);
		case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
			return DecideButton(Dev::kMouse, 1, a_msg != WM_RBUTTONUP);
		case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
			return DecideButton(Dev::kMouse, 2, a_msg != WM_MBUTTONUP);
		case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
			return DecideButton(Dev::kMouse, GET_XBUTTON_WPARAM(a_wp) == XBUTTON1 ? 3u : 4u, a_msg != WM_XBUTTONUP);
		case WM_MOUSEWHEEL:
			if (!open) { return false; }
			Enqueue({ Record::Kind::kMouseWheel, 0, false, 0.0f, static_cast<float>(GET_WHEEL_DELTA_WPARAM(a_wp)) / WHEEL_DELTA });
			return true;
		case WM_MOUSEHWHEEL:
			if (!open) { return false; }
			Enqueue({ Record::Kind::kMouseWheel, 0, false, static_cast<float>(GET_WHEEL_DELTA_WPARAM(a_wp)) / WHEEL_DELTA, 0.0f });
			return true;
		case WM_SETCURSOR:
			// While the menu is up ImGui draws the only cursor; Unreal re-sets its own arrow on every WM_SETCURSOR,
			// which put a second pointer on screen beside ImGui's (seen on the first M2 run, 2026-09-26).
			if (!open || LOWORD(a_lp) != HTCLIENT) { return false; }
			::SetCursor(nullptr);
			return true;
		case WM_INPUT:
			if (!open) { return false; }
			{
				// Unreal's mouse look reads raw input; swallowing it is what stops the camera. The delta is
				// kept as the cursor's fallback source (see g_lastAbsMoveMs).
				//
				// ONLY the mouse (and keyboard) are swallowed. A controller the game reads as a raw HID device
				// arrives here too, and taking its reports is a different thing from stopping the camera - the
				// owner lost the controller after closing the menu on the first M2 run (2026-09-26).
				RAWINPUTHEADER head{};
				UINT headSize = sizeof(head);
				if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(a_lp), RID_HEADER, &head, &headSize, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) ||
					head.dwType == RIM_TYPEHID)
				{
					return false;
				}
				RAWINPUT ri{};
				UINT size = sizeof(ri);
				if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(a_lp), RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1) &&
					ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) &&
					(ri.data.mouse.lLastX || ri.data.mouse.lLastY) &&
					NowMs() - g_lastAbsMoveMs.load(std::memory_order_relaxed) > 150)
				{
					Enqueue({ Record::Kind::kMouseMove, 0, false, static_cast<float>(ri.data.mouse.lLastX), static_cast<float>(ri.data.mouse.lLastY) });
				}
			}
			return true;
		default:
			return false;
		}
	}

	void PollGamepad()
	{
		const bool open = renderer::IsMainWindowVisible();

		// A page that took the sticks gets them taken back the moment the menu is not up, so a mod that
		// forgets to release them cannot leave navigation dead (1.9.5).
		if (!open && g_sticksCaptured.load(std::memory_order_acquire)) { SetSticksCaptured(false); }

		// Driver presses and characters first, one step per frame.
		{
			std::scoped_lock l(g_injectLock);
			for (auto it = g_injected.begin(); it != g_injected.end();)
			{
				if (!it->downSent) { DecideButton(it->dev, it->code, true); it->downSent = true; ++it; continue; }
				if (--it->framesLeft > 0) { ++it; continue; }
				DecideButton(it->dev, it->code, false);
				it = g_injected.erase(it);
			}
			if (!g_injectedChars.empty())
			{
				if (open) { Enqueue({ Record::Kind::kCharacter, g_injectedChars.front(), true, 0.0f, 0.0f }); }
				g_injectedChars.erase(g_injectedChars.begin());
			}
		}

		if (open) { ::ClipCursor(nullptr); }   // Unreal clips the cursor to the viewport for mouse look; the menu needs it free

		// The pad is read ONLY while the menu is up. Steam Input answers XInput inside the game process, and a second
		// reader polling it every frame of normal play is the first suspect for the controller not working in the game
		// after the menu closed (the owner, first M2 run, 2026-09-26). Nothing the framework does with the pad needs it
		// while the menu is down - no controller button opens it yet - so the reads stop, and every button the menu saw
		// is released so nothing is left held.
		if (!open)
		{
			if (g_padButtons != 0)
			{
				for (std::uint32_t bit = 1; bit <= 0x8000; bit <<= 1)
				{
					if (g_padButtons & bit) { DecideButton(Dev::kGamepad, bit, false); }
				}
				g_padButtons = 0;
			}
			for (float& s : g_padSent) { s = 0.0f; }
			return;
		}
		ResolveXInput();
		if (!g_xinput) { return; }
		XINPUT_STATE st{};
		WORD buttons = 0;
		float axes[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		const DWORD rc = g_xinput(0, &st);
		{
			// Observability (rule 31): the pad's connection state as a transition, and a heartbeat of how many reads
			// have been made, so "no controller events" can be told apart from "no reads at all".
			static DWORD s_lastRc = 0xFFFFFFFFu;
			static std::uint64_t s_reads = 0;
			++s_reads;
			if (rc != s_lastRc)
			{
				logger::info("input: XInput user 0 is {} (read #{}, code {})", rc == ERROR_SUCCESS ? "connected" : "not connected", s_reads, rc);
				s_lastRc = rc;
			}
			else if (s_reads % 3600 == 0)
			{
				logger::debug("input: {} XInput reads so far", s_reads);
			}
		}
		if (rc == ERROR_SUCCESS)
		{
			buttons = st.Gamepad.wButtons;
			const auto norm = [](SHORT v) { return std::clamp(static_cast<float>(v) / 32767.0f, -1.0f, 1.0f); };
			axes[0] = norm(st.Gamepad.sThumbLX);
			axes[1] = norm(st.Gamepad.sThumbLY);   // XInput is y-up, as Skyrim's thumbstick events were
			axes[2] = norm(st.Gamepad.sThumbRX);
			axes[3] = norm(st.Gamepad.sThumbRY);
		}
		// A pad that disconnects releases everything it held.
		const WORD changed = static_cast<WORD>(buttons ^ g_padButtons);
		for (std::uint32_t bit = 1; bit <= 0x8000; bit <<= 1)
		{
			if (changed & bit) { DecideButton(Dev::kGamepad, bit, (buttons & bit) != 0); }
		}
		g_padButtons = buttons;

		if (open)
		{
			for (std::uint32_t stick = 0; stick < 2; ++stick)
			{
				const float x = axes[stick * 2], y = axes[stick * 2 + 1];
				if (std::fabs(x - g_padSent[stick * 2]) > 0.01f || std::fabs(y - g_padSent[stick * 2 + 1]) > 0.01f)
				{
					g_padSent[stick * 2] = x;
					g_padSent[stick * 2 + 1] = y;
					Enqueue({ Record::Kind::kThumbstick, stick, false, x, y });
				}
			}
		}
		else
		{
			for (float& s : g_padSent) { s = 0.0f; }
		}
	}

	// ---- keys ImGui believes are held --------------------------------------------------
	// A STUCK ESCAPE (the owner, 2026-09-21: clicking a text box "is not letting me delete the word anymore ...
	// it did actually require me to press escape just now, and it started typing again"). The log showed 62
	// text-field deaths with "keys this frame: Escape(d)" - ImGui thought Escape was HELD. Escape closes this
	// menu, and the release arrives after the menu is hidden, so ImGui never heard it; a held key repeats, and
	// every text field opened after that was cancelled by the repeat about 40 ms later (Escape is InputText's
	// cancel). Pressing Escape again delivered a release and "fixed" it, exactly as reported.
	// So every keyboard key sent DOWN is remembered, and each frame any key Windows reports as UP is released
	// in ImGui too - whatever path lost its real release. Keys younger than 250 ms are left alone, so a
	// driver-injected press (which Windows never sees) still lasts its intended frames.
	struct HeldKey { std::uint32_t scancode; std::chrono::steady_clock::time_point since; };
	std::unordered_map<int, HeldKey> g_heldKeys;   // ImGuiKey -> scancode it came from
	void ReleaseStuckKeys(ImGuiIO& a_io)
	{
		if (g_heldKeys.empty()) { return; }
		const auto now = std::chrono::steady_clock::now();
		for (auto it = g_heldKeys.begin(); it != g_heldKeys.end();)
		{
			if (now - it->second.since < std::chrono::milliseconds(250)) { ++it; continue; }
			const std::uint32_t sc = it->second.scancode;
			const UINT scan = (sc & 0x80) ? (0xE000u | (sc & 0x7Fu)) : sc;   // DirectInput 0xC8 = E0 48
			const UINT vk = ::MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX);
			if (vk != 0 && (::GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) == 0)
			{
				const auto key = static_cast<ImGuiKey>(it->first);
				a_io.AddKeyEvent(key, false);
				if (key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift) { a_io.AddKeyEvent(ImGuiMod_Shift, false); }
				else if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl) { a_io.AddKeyEvent(ImGuiMod_Ctrl, false); }
				else if (key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt) { a_io.AddKeyEvent(ImGuiMod_Alt, false); }
				logger::info("input: key {} (scan 0x{:02X}) was still held in the menu but is up on the keyboard - "
							 "its release was lost (usually because it closed the menu); released it",
							 ImGui::GetKeyName(key), sc);
				it = g_heldKeys.erase(it);
			}
			else { ++it; }
		}
	}

	void ProcessQueuedEvents()
	{
		std::vector<Record> drained;
		{
			std::scoped_lock lock(g_queueLock);
			// Deferred records: count down, and promote the ones that are due into this drain
			// AFTER everything already queued, so a press never overtakes the move before it.
			for (auto it = g_deferred.begin(); it != g_deferred.end();)
			{
				if (--it->framesLeft <= 0)
				{
					g_queue.push_back(it->record);
					it = g_deferred.erase(it);
				}
				else
				{
					++it;
				}
			}
			drained.swap(g_queue);
		}

		ImGuiIO& io = ImGui::GetIO();
		const ImVec2 display = io.DisplaySize;
		// Re-read after every NoteDevice below: the press that SWITCHES the mode must be judged by the mode it
		// switched to. Reading it once at the top dropped the first D-pad press after keyboard use (2026-09-26).
		bool controllerMode = UsingController();
		ReleaseStuckKeys(io);

		for (const Record& record : drained)
		{
			switch (record.kind)
			{
			case Record::Kind::kMouseMove:
				if (std::fabs(record.x) > kMouseMoveThreshold || std::fabs(record.y) > kMouseMoveThreshold)
				{
					NoteDevice(Device::kKeyboardMouse);
				}
				g_cursorX += record.x;
				g_cursorY += record.y;
				g_cursorX = g_cursorX < 0.0f ? 0.0f : (g_cursorX > display.x - 1.0f ? display.x - 1.0f : g_cursorX);
				g_cursorY = g_cursorY < 0.0f ? 0.0f : (g_cursorY > display.y - 1.0f ? display.y - 1.0f : g_cursorY);
				break;
			case Record::Kind::kCursorSet:
				// Absolute placement from a driver (DevBench). Counts as mouse use so the shell
				// switches to keyboard/mouse presentation, exactly as a real move would.
				NoteDevice(Device::kKeyboardMouse);
				g_cursorX = record.x < 0.0f ? 0.0f : (record.x > display.x - 1.0f ? display.x - 1.0f : record.x);
				g_cursorY = record.y < 0.0f ? 0.0f : (record.y > display.y - 1.0f ? display.y - 1.0f : record.y);
				// Publish the position NOW, ahead of any button record queued behind it in this
				// same drain. The per-frame AddMousePosEvent runs after the loop, so without this
				// a driver's press would reach ImGui before the move and land on the old spot.
				io.AddMousePosEvent(g_cursorX, g_cursorY);
				break;
			case Record::Kind::kMouseAbs:
				// The OS cursor's own position (Oblivion Remastered's WM_MOUSEMOVE). Real movement past the
				// threshold is deliberate mouse use, exactly as a Skyrim MouseMoveEvent was.
				if (std::fabs(record.x - g_cursorX) > kMouseMoveThreshold || std::fabs(record.y - g_cursorY) > kMouseMoveThreshold)
				{
					NoteDevice(Device::kKeyboardMouse);
				}
				g_cursorX = record.x < 0.0f ? 0.0f : (record.x > display.x - 1.0f ? display.x - 1.0f : record.x);
				g_cursorY = record.y < 0.0f ? 0.0f : (record.y > display.y - 1.0f ? display.y - 1.0f : record.y);
				break;
			case Record::Kind::kMouseButton:
				if (record.down) { NoteDevice(Device::kKeyboardMouse); }
				if (record.code < ImGuiMouseButton_COUNT)
				{
					io.AddMouseButtonEvent(static_cast<int>(record.code), record.down);
				}
				break;
			case Record::Kind::kMouseWheel:
				io.AddMouseWheelEvent(record.x, record.y);
				break;
			case Record::Kind::kKeyboard:
				{
					if (record.down) { NoteDevice(Device::kKeyboardMouse); }
					// A key typed into a text field is text, not a command: F (favourite) and Page Up / Down
					// (tabs) must not fire while the search bar or a mod's text box is being typed into.
					if (record.down && !renderer::WantsTextInput()) { bindings::RaiseAllFor(record.code, false); }
					const ImGuiKey key = ScancodeToImGuiKey(record.code);
					if (key != ImGuiKey_None)
					{
						io.AddKeyEvent(key, record.down);
						if (record.down) { g_heldKeys[static_cast<int>(key)] = HeldKey{ record.code, std::chrono::steady_clock::now() }; }
						else { g_heldKeys.erase(static_cast<int>(key)); }

						// Modifier flags tracked explicitly - "modifier keys are not left/right
						// side conscious" (survey, ModExplorerMenu's translation notes).
						if (key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift)
							io.AddKeyEvent(ImGuiMod_Shift, record.down);
						else if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl)
							io.AddKeyEvent(ImGuiMod_Ctrl, record.down);
						else if (key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt)
							io.AddKeyEvent(ImGuiMod_Alt, record.down);
					}
					break;
				}
			case Record::Kind::kGamepad:
				// Observability (rule 31): log EVERY gamepad event that reaches this loop, so a
				// live DevBench-monitored test can tell apart "no gamepad events arrive at all"
				// (a game-level / input-device-mode problem - e.g. the Auto Input Switch mod not
				// present to route the device) from "events arrive but nav does not respond" (an
				// ImGui-side problem). If these lines are ABSENT while pressing buttons with the
				// menu open, the events are not reaching the framework.
				{
					if (record.code == 0x0040) { g_stickClicked[0].store(record.down, std::memory_order_relaxed); }
					if (record.code == 0x0080) { g_stickClicked[1].store(record.down, std::memory_order_relaxed); }
					const ImGuiKey key = GamepadMaskToImGuiKey(record.code);
					logger::debug("gamepad event: code=0x{:04X} down={} controllerMode={} -> imguiKey={}",
								  record.code, record.down, controllerMode, static_cast<int>(key));
					if (record.down) { NoteDevice(Device::kGamepad); } controllerMode = UsingController();
					// A binding whose action has no ImGui key of its own raises a flag the renderer
					// consumes: the tab steps, the context menu and the favourite command are the
					// framework's own commands, not navigation.
					// EVERY action this input is bound to, not just the first. Two actions may share a
					// control when they can never be live together - Y is the on-screen keyboard's
					// backspace AND "open a mod's options" - but FromGamepad returns the first match
					// in enum order, which is the backspace, so the context menu was never raised
					// (the owner, 2026-09-19: "pressing Y doesn't, even though it's bound to it").
					if (record.down) { bindings::RaiseAllFor(record.code, true); }
					// 1.8.9: the on-screen keyboard takes the pad while it is open (D-pad, A, B, X, Y), and
					// takes the A that opens it on a highlighted text box; everything else falls through.
					if (controllerMode && keyboard::HandleGamepad(record.code, record.down))
					{
						break;
					}
					// A TEXT FIELD OWNS THE D-PAD WHILE IT IS ACTIVE (2026-09-19). ImGui's InputText
					// claims the keyboard arrow keys while you type, but NOT the gamepad D-pad - so a
					// D-pad press moved nav to another item, and moving nav off an active text box
					// deactivates it. From the player's side the box simply went dead, and the
					// on-screen keyboard closed with it, because the D-pad is exactly what you press
					// to walk that keyboard. Proven from the log: the frame the field died carried
					// "GamepadDpadDown(d)" and a fresh navJustMovedTo id, and nothing else.
					if (controllerMode && key != ImGuiKey_None && !TextFieldHasTheKeyboard(key))
					{
						io.AddKeyEvent(key, record.down);
					}
					else if (controllerMode && key == ImGuiKey_GamepadFaceRight && record.down &&
							 TextFieldHasTheKeyboard(key))
					{
						// Kept from ImGui above; the renderer turns it into "let go of the box".
						g_textFieldCancel.store(true, std::memory_order_release);
					}
				}
				break;
			case Record::Kind::kThumbstick:
				if (std::fabs(record.x) > kStickThreshold || std::fabs(record.y) > kStickThreshold)
				{
					NoteDevice(Device::kGamepad);
					controllerMode = UsingController();
				}
				// Kept raw for GetStick() before anything is decided about navigation.
				if (record.code < 2)
				{
					g_stickX[record.code].store(record.x, std::memory_order_relaxed);
					g_stickY[record.code].store(record.y, std::memory_order_relaxed);
				}
				// Left stick -> ImGui gamepad-nav analog axes, so the stick moves the menu
				// selection like the D-pad (the author used the stick to "switch menus"; it was not
				// captured). Deadzone stops a resting stick drifting nav. y>0 = up in Skyrim's
				// thumbstick convention; if the live test shows it inverted, flip Up/Down.
				// The controller scheme (author's spec, 2026-08-31): the LEFT stick moves through
				// the list and across to the options with no button press; A takes hold of a slider;
				// the RIGHT stick then moves it. ImGui drives BOTH navigation and value tweaking from
				// the same LStick nav axes, so exactly one stick is wired to them per frame - whichever
				// the scheme says is in charge:  nothing being edited -> LEFT (navigate);  an item
				// taken hold of -> RIGHT (move the value). The idle stick is explicitly released so a
				// resting-but-off-centre stick cannot leave a nav axis stuck down.
				if (controllerMode && keyboard::HandleStick(record.code, record.x, record.y))
				{
					break;   // 1.8.9: the keyboard has the left stick while it is open; 1.9.0: and swallows the right one
				}
				if (controllerMode)
				{
					const bool editing = g_itemActive.load(std::memory_order_relaxed);
					const bool isLeftStick = record.code == 0;
					// A consumer holding the sticks takes BOTH out of navigation, so the selection
					// cannot move while the player is handling whatever the page gave them (1.9.5).
					const bool captured = g_sticksCaptured.load(std::memory_order_relaxed);
					const bool inCharge = captured ? false : (editing ? !isLeftStick : isLeftStick);
					constexpr float dz = 0.35f;
					float sx = inCharge ? record.x : 0.0f, sy = inCharge ? record.y : 0.0f;
					// Same reasoning as the D-pad above: the nav axes are what move focus, so while a
					// text field holds the keyboard the sticks are reported as centred.
					const bool typing = GImGui && GImGui->ActiveId != 0 && keyboard::IsTextField(GImGui->ActiveId);
					if (typing) { sx = 0.0f; sy = 0.0f; }
					io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft,  sx < -dz, sx < -dz ? -sx : 0.0f);
					io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, sx >  dz, sx >  dz ?  sx : 0.0f);
					io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp,    sy >  dz, sy >  dz ?  sy : 0.0f);
					io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown,  sy < -dz, sy < -dz ? -sy : 0.0f);
					logger::debug("thumbstick({}): x={:.2f} y={:.2f} {} (editing={})",
								  isLeftStick ? "L" : "R", record.x, record.y,
								  inCharge ? "-> nav" : "(idle this frame)", editing);
				}
				break;
			case Record::Kind::kCharacter:
				io.AddInputCharacter(record.code);
				break;
			}
		}

		// One authoritative cursor position per frame, movement or not. The Win32 backend's
		// fallback poll pushes the OS cursor position (which the game recentres at will) into
		// the same event queue every frame; on frames where we stayed silent that stale
		// position won, which is exactly the flicker/teleport of the 1.1.0 smoke test. Being
		// unconditionally last - paired with trickle-off (set at init) - means the software
		// cursor is the only position ImGui ever acts on.
		io.AddMousePosEvent(g_cursorX, g_cursorY);
		g_cursorMirrorX.store(g_cursorX, std::memory_order_relaxed);
		g_cursorMirrorY.store(g_cursorY, std::memory_order_relaxed);
	}

	void OnMenuOpened()
	{
		const ImVec2 display = ImGui::GetIO().DisplaySize;
		g_cursorX = display.x * 0.5f;
		g_cursorY = display.y * 0.5f;
		// Oblivion Remastered: the OS cursor is real input here (WM_MOUSEMOVE), so the menu's cursor starts where the
		// mouse already is when it is inside the window; otherwise the first movement would jump it from the centre.
		if (HWND w = static_cast<HWND>(renderer::GetGameWindow()))
		{
			POINT pt{};
			RECT rc{};
			if (::GetCursorPos(&pt) && ::ScreenToClient(w, &pt) && ::GetClientRect(w, &rc) && ::PtInRect(&rc, pt))
			{
				g_cursorX = static_cast<float>(pt.x);
				g_cursorY = static_cast<float>(pt.y);
				g_lastAbsX = pt.x;
				g_lastAbsY = pt.y;
			}
		}
		ImGui::GetIO().AddMousePosEvent(g_cursorX, g_cursorY);

		// Nothing is held when the menu opens: a release that arrived while it was hidden is gone for good.
		ImGui::GetIO().ClearInputKeys();
		g_heldKeys.clear();

		std::scoped_lock lock(g_queueLock);
		g_queue.clear();

		logger::debug("menu opened: cursor centred at ({:.0f}, {:.0f}), stale queue cleared", g_cursorX, g_cursorY);
	}

	void BeginRebindToggleKey()
	{
		g_awaitingRebind.store(true, std::memory_order_release);
		logger::info("awaiting menu toggle-key rebind - next keyboard key wins, Escape cancels");
	}

	bool IsAwaitingRebind()
	{
		return g_awaitingRebind.load(std::memory_order_acquire);
	}

	Device LastDevice()
	{
		return g_lastDevice.load(std::memory_order_relaxed);
	}

	bool UsingController()
	{
		// Unknown means nothing deliberate has happened yet, and that resolves to KEYBOARD: this
		// is a PC framework, the menu is opened with a key, and guessing "controller" for a player
		// who has not touched one would hand them prompts for a device they may not own.
		return g_lastDevice.load(std::memory_order_relaxed) == Device::kGamepad;
	}

	float SecondsSinceLastDevice()
	{
		const auto at = g_lastDeviceAt.load(std::memory_order_relaxed);
		if (at == std::chrono::steady_clock::time_point{}) { return -1.0f; }
		return std::chrono::duration<float>(std::chrono::steady_clock::now() - at).count();
	}

	void SetItemActive(bool a_active)
	{
		g_itemActive.store(a_active, std::memory_order_relaxed);
	}

	void ArmKeyCapture()
	{
		g_lastCaptured.store(-1, std::memory_order_release);
		g_captureArmed.store(true, std::memory_order_release);
		logger::info("keybind capture armed - next keyboard/gamepad press will be recorded (not consumed)");
	}

	void CancelKeyCapture()
	{
		g_captureArmed.store(false, std::memory_order_release);
	}

	bool IsKeyCaptureArmed()
	{
		return g_captureArmed.load(std::memory_order_acquire);
	}

	std::int64_t LastCapturedKey()
	{
		return g_lastCaptured.load(std::memory_order_acquire);
	}

	void SetCursorAbsolute(float a_x, float a_y)
	{
		Enqueue({ Record::Kind::kCursorSet, 0, false, a_x, a_y });
	}

	void QueueMouseButton(std::uint32_t a_button, bool a_down)
	{
		Enqueue({ Record::Kind::kMouseButton, a_button, a_down, 0.0f, 0.0f });
	}

	void QueueMouseClick(std::uint32_t a_button)
	{
		std::scoped_lock lock(g_queueLock);
		g_deferred.push_back({ 1, { Record::Kind::kMouseButton, a_button, true, 0.0f, 0.0f } });
		g_deferred.push_back({ 3, { Record::Kind::kMouseButton, a_button, false, 0.0f, 0.0f } });
	}

	// Driver-side key press (DirectInput scan code) and text, queued as the SAME records the game's
	// own events become - so a headless test exercises the translation and ImGui exactly as a
	// keyboard would, from the record queue onward. Down and up land on separate frames.

	void InjectPress(std::uint32_t a_device, std::uint32_t a_code, int a_holdFrames)
	{
		const Dev dev = a_device == 2 ? Dev::kGamepad : (a_device == 1 ? Dev::kMouse : Dev::kKeyboard);
		std::scoped_lock l(g_injectLock);
		g_injected.push_back({ dev, a_code, a_holdFrames < 1 ? 1 : (a_holdFrames > 600 ? 600 : a_holdFrames), false });
	}
	void InjectText(const std::string& a_utf8)
	{
		std::scoped_lock l(g_injectLock);
		for (unsigned char c : a_utf8) { g_injectedChars.push_back(static_cast<std::uint32_t>(c)); }
	}


	void QueueKey(std::uint32_t a_scancode)
	{
		std::scoped_lock lock(g_queueLock);
		g_deferred.push_back({ 1, { Record::Kind::kKeyboard, a_scancode, true, 0.0f, 0.0f } });
		g_deferred.push_back({ 3, { Record::Kind::kKeyboard, a_scancode, false, 0.0f, 0.0f } });
	}

	void QueueText(const std::string& a_utf8)
	{
		std::scoped_lock lock(g_queueLock);
		int frame = 1;
		for (unsigned char c : a_utf8) {
			g_deferred.push_back({ frame++, { Record::Kind::kCharacter, static_cast<std::uint32_t>(c), true, 0.0f, 0.0f } });
		}
	}

	void SetSticksCaptured(bool a_captured)
	{
		const bool was = g_sticksCaptured.exchange(a_captured, std::memory_order_release);
		if (was != a_captured)
		{
			// itemActive is reported with the release because it decides which stick drives
			// navigation: while an item is being edited the RIGHT stick moves it and the LEFT one is
			// held off. If it is still true after a page lets go, the left stick stays dead and the
			// D-pad appears to be the only thing that works (the owner, 2026-09-19).
			logger::info("input: the thumbsticks are {} by a mod's page (itemActive={})",
						 a_captured ? "held" : "released", g_itemActive.load(std::memory_order_relaxed));
		}
	}

	bool AreSticksCaptured()
	{
		return g_sticksCaptured.load(std::memory_order_acquire);
	}

	void GetStick(int a_which, float& a_x, float& a_y, bool& a_clicked, bool& a_live)
	{
		const int i = (a_which == 1) ? 1 : 0;
		a_x = g_stickX[i].load(std::memory_order_relaxed);
		a_y = g_stickY[i].load(std::memory_order_relaxed);
		a_clicked = g_stickClicked[i].load(std::memory_order_relaxed);
		a_live = std::fabs(a_x) > kStickThreshold || std::fabs(a_y) > kStickThreshold;
	}

	bool TakeTextFieldCancel()
	{
		return g_textFieldCancel.exchange(false, std::memory_order_acq_rel);
	}

	void GetCursor(float& a_x, float& a_y)
	{
		a_x = g_cursorMirrorX.load(std::memory_order_relaxed);
		a_y = g_cursorMirrorY.load(std::memory_order_relaxed);
	}
}
