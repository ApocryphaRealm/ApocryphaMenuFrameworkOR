#include "PCH.h"

#include "Keyboard.h"

#include "Input.h"
#include "Renderer.h"
#include "Settings.h"
#include "Strings.h"
#include "Logger.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace keyboard
{
	using strings::TR;

	namespace
	{
		// ---- the layout ----------------------------------------------------------------------
		// Four character rows and one row of actions. The action row is index 4; its columns are
		// fixed below so the D-pad lands on them predictably.
		const char* kRows[4] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm-'" };
		constexpr int kActionRow = 4;
		enum Action : int { kShift = 0, kSpace, kBack, kClear, kDone, kActionCount };
		constexpr int kRowCount = 5;

		int RowLength(int a_row) { return a_row == kActionRow ? kActionCount : static_cast<int>(std::strlen(kRows[a_row])); }

		// ---- state (render thread; the input translation runs on it too, before NewFrame) ------
		std::unordered_set<std::uint32_t> g_fieldsThisFrame;   // text-field item ids seen this frame
		std::unordered_set<std::uint32_t> g_fieldsLastFrame;
		std::uint32_t g_target = 0;      // the text field being typed into
		std::atomic<bool> g_capturing{ false };   // read by the input translation
		bool g_open = false;
		bool g_forced = false;           // AMF_ShowKeyboard(): open even outside controller mode
		bool g_shift = false;
		int g_row = 1, g_col = 0;
		std::chrono::steady_clock::time_point g_stickAt{};
		int g_stickDirX = 0, g_stickDirY = 0;
		std::uint32_t g_typed = 0, g_activations = 0;

		bool IsTextField_(std::uint32_t a_id)
		{
			return a_id != 0 && (g_fieldsLastFrame.count(a_id) != 0 || g_fieldsThisFrame.count(a_id) != 0);
		}

		bool SettingOn() { return settings::Get().onScreenKeyboard; }

		void Move(int a_dx, int a_dy)
		{
			if (a_dy != 0)
			{
				g_row = (g_row + a_dy + kRowCount) % kRowCount;
				// Keep the column inside the new row: the rows are not all the same length.
				const int len = RowLength(g_row);
				const int prevLen = RowLength((g_row - a_dy + kRowCount) % kRowCount);
				if (prevLen > 0) { g_col = static_cast<int>(static_cast<float>(g_col) / prevLen * len); }
				if (g_col >= len) { g_col = len - 1; }
				if (g_col < 0) { g_col = 0; }
			}
			if (a_dx != 0)
			{
				const int len = RowLength(g_row);
				g_col = (g_col + a_dx + len) % len;
			}
		}

		void SendChar(char a_c)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddInputCharacter(static_cast<unsigned int>(static_cast<unsigned char>(a_c)));
			++g_typed;
		}

		// A synthetic key press is RELEASED ONE FRAME LATER, never in the same call. The framework runs
		// ImGui with ConfigInputTrickleEventQueue OFF (Renderer.cpp), so a down and an up queued together
		// are applied in the same NewFrame and the key is never seen down - Back, Clear and Y did nothing
		// (the owner, 2026-09-18). The release is queued from Draw() once the press has been applied.
		struct PendingRelease { ImGuiKey key; bool ctrl; int atFrame; };
		std::vector<PendingRelease> g_pendingRelease;

		void SendKeyTap(ImGuiKey a_key, bool a_ctrl = false)
		{
			ImGuiIO& io = ImGui::GetIO();
			if (a_ctrl) { io.AddKeyEvent(ImGuiMod_Ctrl, true); }
			io.AddKeyEvent(a_key, true);
			g_pendingRelease.push_back({ a_key, a_ctrl, ImGui::GetFrameCount() + 1 });
		}

		void FlushReleases()
		{
			const int frame = ImGui::GetFrameCount();
			ImGuiIO& io = ImGui::GetIO();
			for (auto it = g_pendingRelease.begin(); it != g_pendingRelease.end();)
			{
				if (frame >= it->atFrame)
				{
					io.AddKeyEvent(it->key, false);
					if (it->ctrl) { io.AddKeyEvent(ImGuiMod_Ctrl, false); }
					it = g_pendingRelease.erase(it);
				}
				else { ++it; }
			}
		}

		void Press()
		{
			if (g_row != kActionRow)
			{
				char c = kRows[g_row][g_col];
				if (g_shift && c >= 'a' && c <= 'z') { c = static_cast<char>(c - 'a' + 'A'); }
				SendChar(c);
				return;
			}
			switch (g_col)
			{
			case kShift: g_shift = !g_shift; break;
			case kSpace: SendChar(' '); break;
			case kBack:  SendKeyTap(ImGuiKey_Backspace); break;
			case kClear:
				// Select everything through the edit state itself, then one Backspace deletes it; two
				// taps (Ctrl+A, Backspace) cannot be sequenced within one frame.
				if (ImGuiInputTextState* st = ImGui::GetInputTextState(g_target)) { st->SelectAll(); }
				SendKeyTap(ImGuiKey_Backspace);
				break;
			case kDone:  Hide(); break;
			default: break;
			}
		}

		void Close(const char* a_why)
		{
			if (g_open) { logger::debug("keyboard: closed ({})", a_why); }
			g_open = false;
			g_forced = false;
			g_capturing = false;
		}

		// Ask ImGui to put the target text field into input mode on its next frame - the same
		// thing its own "input" button (X on a pad) does, done for A here because a highlighted
		// search box is what a player expects A to open.
		void ActivateTarget()
		{
			ImGuiContext& g = *GImGui;
			g.NavNextActivateId = g_target;
			g.NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
			++g_activations;
		}

		// The current text of the target, from ImGui's own edit state (only while it is active).
		std::string TargetText()
		{
			ImGuiContext& g = *GImGui;
			if (g_target == 0 || g.ActiveId != g_target) { return {}; }
			ImGuiInputTextState* st = ImGui::GetInputTextState(g_target);
			if (!st) { return {}; }
			return std::string(st->TextA.Data ? st->TextA.Data : "", st->TextA.Data ? std::strlen(st->TextA.Data) : 0);
		}
	}

	bool IsTextField(std::uint32_t a_id)
	{
		return IsTextField_(a_id);
	}

	bool WasSubmittedLastFrame(std::uint32_t a_id)
	{
		return a_id != 0 && g_fieldsLastFrame.count(a_id) != 0;
	}

	void NoteTextField(std::uint32_t a_itemId)
	{
		if (a_itemId != 0) { g_fieldsThisFrame.insert(a_itemId); }
	}

	bool Capturing() { return g_capturing.load(std::memory_order_relaxed); }

	void Show() { g_forced = true; g_open = true; g_capturing = true; if (g_target == 0) { g_target = GImGui ? GImGui->NavId : 0; } }
	void Hide() { Close("Done / AMF_HideKeyboard"); }

	bool HandleGamepad(std::uint32_t a_mask, bool a_down)
	{
		if (!SettingOn()) { return false; }
		ImGuiContext* g = GImGui;
		if (!g) { return false; }

		// Not capturing: A on a highlighted text field opens the keyboard and starts input.
		if (!g_open)
		{
			if (a_mask == 0x1000 && a_down && IsTextField(g->NavId) && renderer::IsMainWindowVisible())
			{
				g_target = g->NavId;
				g_open = true;
				g_capturing = true;
				g_row = 1; g_col = 0;
				ActivateTarget();
				logger::debug("keyboard: opened for text field 0x{:08X}", g_target);
				return true;   // the A press is ours; ImGui would otherwise 'tweak' the field
			}
			return false;
		}

		// Capturing: the pad is the keyboard's.
		switch (a_mask)
		{
		case 0x0001: if (a_down) { Move(0, -1); } return true;   // D-pad up
		case 0x0002: if (a_down) { Move(0, +1); } return true;   // down
		case 0x0004: if (a_down) { Move(-1, 0); } return true;   // left
		case 0x0008: if (a_down) { Move(+1, 0); } return true;   // right
		case 0x1000: if (a_down) { Press(); } return true;       // A = press the key
		case 0x2000:                                              // B = back to the search box
			if (a_down)
			{
				Close("B");
				// The highlight goes back onto the text box, which stays in input mode so the
				// page's own navigation resumes from there.
				ImGui::SetNavID(g_target, g->NavLayer, 0, ImRect());
			}
			return true;
		case 0x4000: if (a_down) { g_shift = !g_shift; } return true;   // X = shift
		case 0x8000: if (a_down) { SendKeyTap(ImGuiKey_Backspace); } return true;   // Y = backspace
		default:
			return false;   // bumpers, triggers, Start: the page's
		}
	}

	bool HandleStick(std::uint32_t a_stick, float a_x, float a_y)
	{
		if (!g_open) { return false; }
		// The RIGHT stick is swallowed outright: with the target text box active the page would otherwise
		// hand it to ImGui as the value-tweak axis (the owner, 2026-09-18: lock the pad to the keyboard
		// until they exit or switch to mouse).
		if (a_stick != 0) { return true; }
		// The left stick works as a repeating D-pad: a push past the deadzone steps once, and
		// holding it repeats every 180 ms.
		constexpr float dz = 0.6F;
		const int dx = a_x > dz ? 1 : (a_x < -dz ? -1 : 0);
		const int dy = a_y > dz ? -1 : (a_y < -dz ? 1 : 0);   // y>0 is up in Skyrim's convention
		const auto now = std::chrono::steady_clock::now();
		if (dx == 0 && dy == 0) { g_stickDirX = g_stickDirY = 0; return true; }
		const bool changed = dx != g_stickDirX || dy != g_stickDirY;
		const bool due = std::chrono::duration<double, std::milli>(now - g_stickAt).count() > 180.0;
		if (changed || due)
		{
			Move(dx, dy);
			g_stickAt = now;
			g_stickDirX = dx; g_stickDirY = dy;
		}
		return true;   // swallowed either way: the stick must not also move the page's highlight
	}

	void Draw()
	{
		FlushReleases();
		// This frame's text fields are complete (the page has drawn); keep them for the input
		// translation, which runs before the next frame draws.
		g_fieldsLastFrame.swap(g_fieldsThisFrame);
		g_fieldsThisFrame.clear();

		if (!SettingOn() || !renderer::IsMainWindowVisible()) { Close("setting off or window closed"); return; }
		if (!g_open) { return; }

		ImGuiContext& g = *GImGui;
		// The target lost input mode to something we did not do (a mouse click elsewhere, the
		// page rebuilt): the keyboard closes rather than typing into nothing. It is given a few
		// frames after opening, because activation lands on the frame after the request.
		static int s_graceFrames = 0;
		if (g.ActiveId == g_target) { s_graceFrames = 0; }
		else if (++s_graceFrames > 30 && !g_forced) { Close("the text field is no longer taking input"); s_graceFrames = 0; return; }

		const ImGuiIO& io = ImGui::GetIO();
		const float k = ImGui::GetFrameHeight() * 1.6F;
		const float gap = ImGui::GetStyle().ItemSpacing.x;
		const float width = 10.0F * k + 9.0F * gap + ImGui::GetStyle().WindowPadding.x * 2.0F;
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5F, io.DisplaySize.y - k * 0.5F), ImGuiCond_Always, ImVec2(0.5F, 1.0F));
		ImGui::SetNextWindowSize(ImVec2(width, 0.0F), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.92F);
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
									   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
									   ImGuiWindowFlags_NoSavedSettings;
		if (!ImGui::Begin("###amf-keyboard", nullptr, flags)) { ImGui::End(); return; }
		// In front of the framework window: the keyboard is what the player is using, and a tall
		// window otherwise covers its top rows (seen in the first proof, 2026-09-18).
		ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

		ImGui::TextDisabled("%s", TR("AMF_OskHint", "D-pad moves, A types, B goes back to the box, X shift, Y backspace"));
		const ImVec4 hi = ImGui::GetStyleColorVec4(ImGuiCol_NavHighlight);
		for (int r = 0; r < kRowCount; ++r)
		{
			const int len = RowLength(r);
			// centre the shorter rows
			const float rowWidth = (r == kActionRow) ? (k * 1.6F + k * 4.0F + k * 1.8F + k * 1.8F + k * 1.6F + 4.0F * gap) : (len * k + (len - 1) * gap);
			ImGui::SetCursorPosX((ImGui::GetWindowSize().x - rowWidth) * 0.5F);
			for (int c = 0; c < len; ++c)
			{
				const bool cursor = (r == g_row && c == g_col);
				if (cursor) { ImGui::PushStyleColor(ImGuiCol_Button, hi); }
				ImGui::PushID(r * 32 + c);
				bool pressed = false;
				if (r != kActionRow)
				{
					char label[2] = { kRows[r][c], '\0' };
					if (g_shift && label[0] >= 'a' && label[0] <= 'z') { label[0] = static_cast<char>(label[0] - 'a' + 'A'); }
					pressed = ImGui::Button(label, ImVec2(k, k));
				}
				else
				{
					switch (c)
					{
					case kShift: pressed = ImGui::Button(TR("AMF_OskShift", "Shift"), ImVec2(k * 1.6F, k)); break;
					case kSpace: pressed = ImGui::Button(TR("AMF_OskSpace", "Space"), ImVec2(k * 4.0F, k)); break;
					case kBack:  pressed = ImGui::Button(TR("AMF_OskBack", "Back"), ImVec2(k * 1.8F, k)); break;
					case kClear: pressed = ImGui::Button(TR("AMF_OskClear", "Clear"), ImVec2(k * 1.8F, k)); break;
					case kDone:  pressed = ImGui::Button(TR("AMF_OskDone", "Done"), ImVec2(k * 1.6F, k)); break;
					default: break;
					}
				}
				ImGui::PopID();
				if (cursor) { ImGui::PopStyleColor(); }
				if (pressed) { g_row = r; g_col = c; Press(); }
				if (c + 1 < len) { ImGui::SameLine(); }
			}
		}
		ImGui::End();
	}

	std::string StateJson()
	{
		std::string text = TargetText();
		std::string esc;
		for (char ch : text) { if (ch == '"' || ch == '\\') { esc += '\\'; } if (static_cast<unsigned char>(ch) >= 0x20) { esc += ch; } }
		ImGuiContext* g = GImGui;
		char buf[320];
		std::snprintf(buf, sizeof(buf), "{\"open\":%s,\"capturing\":%s,\"forced\":%s,\"target\":\"0x%08X\",\"targetActive\":%s,\"navOnTextField\":%s,\"row\":%d,\"col\":%d,\"shift\":%s,\"typed\":%u,\"activations\":%u,\"fields\":%zu,\"text\":\"%s\"}",
					  g_open ? "true" : "false", Capturing() ? "true" : "false", g_forced ? "true" : "false", g_target,
					  (g && g->ActiveId == g_target && g_target) ? "true" : "false", (g && IsTextField(g->NavId)) ? "true" : "false",
					  g_row, g_col, g_shift ? "true" : "false", g_typed, g_activations, g_fieldsLastFrame.size(), esc.c_str());
		return buf;
	}
}

// The generated cimgui exports call this after every text field they draw (tools/harden-cimgui.py).
void amf_NoteTextField(unsigned int a_itemId)
{
	keyboard::NoteTextField(a_itemId);
}
