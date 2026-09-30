#pragma once

// ============================================================================================================
// A precise slider (the owner, 2026-09-29: "We need to make sure that all of our sliders are precise sliders and they
// don't jump more than one numerical unit per D-pad nudge"). ImGui's SliderBehaviorT moves a keyboard / gamepad nudge by
// 1% of the slider's range whenever the format shows decimals, and also for a "%.0f" slider whose range is wider than
// 100 - so a -150..150 slider stepped 3 per D-pad press. precise::SliderFloat draws ImGui's own slider and, when the
// change came from a keyboard or gamepad nudge (not a mouse drag, and not a number typed in), replaces it with exactly one
// step of the last digit shown: 1 for "%.0f", 0.1 for "%.1f", 0.01 for "%.2f". Held, the nudge repeats as ImGui repeats.
// ============================================================================================================

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace precise
{
	// one unit of the last digit the format shows ("%.0f cm" -> 1, "%.2fx" -> 0.01); 1 when there is no precision
	inline double Step(const char* a_format)
	{
		const char* p = a_format ? std::strstr(a_format, "%.") : nullptr;
		if (p && p[2] >= '0' && p[2] <= '9') {
			return std::pow(10.0, -(p[2] - '0'));
		}
		return 1.0;
	}

	inline bool SliderFloat(const char* a_label, float* a_v, float a_min, float a_max, const char* a_format)
	{
		const float before = *a_v;
		const bool  changed = ImGui::SliderFloat(a_label, a_v, a_min, a_max, a_format);
		if (!changed) return false;
		ImGuiContext& g = *GImGui;
		const ImGuiID id = ImGui::GetItemID();
		const bool nav = g.ActiveId == id && (g.ActiveIdSource == ImGuiInputSource_Keyboard || g.ActiveIdSource == ImGuiInputSource_Gamepad);
		const bool typing = g.InputTextState.ID == id && ImGui::TempInputIsActive(id);
		if (nav && !typing && *a_v != before) {
			const double step = Step(a_format);
			double v = static_cast<double>(before) + (*a_v > before ? step : -step);
			v = std::round(v / step) * step;   // on the grid the format shows
			*a_v = static_cast<float>(std::clamp(v, static_cast<double>(a_min), static_cast<double>(a_max)));
		}
		return true;
	}

	inline bool SliderInt(const char* a_label, int* a_v, int a_min, int a_max, const char* a_format = "%d")
	{
		const int  before = *a_v;
		const bool changed = ImGui::SliderInt(a_label, a_v, a_min, a_max, a_format);
		if (!changed) return false;
		ImGuiContext& g = *GImGui;
		const ImGuiID id = ImGui::GetItemID();
		const bool nav = g.ActiveId == id && (g.ActiveIdSource == ImGuiInputSource_Keyboard || g.ActiveIdSource == ImGuiInputSource_Gamepad);
		const bool typing = g.InputTextState.ID == id && ImGui::TempInputIsActive(id);
		if (nav && !typing && *a_v != before) {
			*a_v = std::clamp(before + (*a_v > before ? 1 : -1), a_min, a_max);
		}
		return true;
	}
}
