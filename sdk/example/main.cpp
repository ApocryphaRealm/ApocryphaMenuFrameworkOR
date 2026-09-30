// SPDX-License-Identifier: MIT
// AMF Example - the smallest complete mod with pages in the Apocrypha Menu Framework (Oblivion Remastered, OBSE64).
// Copy it as the start of your own. Everything it knows about the framework comes from AMF.h.

#include <OBSE/OBSE.h>

#include <imgui.h>   // Dear ImGui 1.90.8 docking - the framework's version (see AMF.h, DRAWING)

#include "AMF.h"
#include "PreciseSlider.h"   // one unit per D-pad nudge (sdk/include)

namespace
{
	float g_speed = 1.0f;
	bool  g_enabled = true;
	bool  g_advanced = false;
	int   g_mode = 0;
	char  g_name[64] = "Adventurer";

	void DrawSettings()
	{
		if (!AMF::UseFrameworkImGui()) {
			return;   // not the framework's ImGui (or not up yet): draw nothing
		}
		ImGui::TextWrapped("A page from another mod, drawn with the ordinary C++ Dear ImGui API through AMF.h.");
		ImGui::Separator();
		ImGui::Checkbox("Enabled", &g_enabled);
		precise::SliderFloat("Speed", &g_speed, 0.5f, 3.0f, "%.2f");   // a D-pad nudge moves 0.01, not 1% of the range
		const char* modes[] = { "Gentle", "Normal", "Brutal" };
		ImGui::Combo("Mode", &g_mode, modes, IM_ARRAYSIZE(modes));
		ImGui::InputText("Name", g_name, sizeof(g_name));
		if (ImGui::Checkbox("Show the Advanced page", &g_advanced)) {
			AMF::SetPageVisible("AMF Example", "Advanced", g_advanced);
		}
		ImGui::Spacing();
		ImGui::TextDisabled("Framework %s, API %u, language %s, input: %s", AMF::Version(), AMF::APIVersion(), AMF::Language(),
			AMF::GetInputMode() == AMF::InputMode::kController ? "controller" : "keyboard/mouse");
	}

	void DrawAdvanced()
	{
		if (!AMF::UseFrameworkImGui()) {
			return;
		}
		ImGui::TextWrapped("Hidden until the switch on the Settings page turns it on (AMF::SetPageVisible).");
		float lx = 0.0f, ly = 0.0f;
		bool  live = false;
		AMF::GetStick(0, &lx, &ly, nullptr, &live);
		ImGui::Text("Left stick: %.2f, %.2f%s", lx, ly, live ? "  (moving)" : "");
	}

	void OnMessage(OBSE::MessagingInterface::Message* a_msg)
	{
		// kPostLoad: every OBSE plugin is loaded, whatever order their file names put them in.
		if (!a_msg || a_msg->type != OBSE::MessagingInterface::kPostLoad) {
			return;
		}
		if (!AMF::IsInstalled()) {
			REX::INFO("AMF Example: the Apocrypha Menu Framework is not installed - no menu page");
			return;
		}
		const bool a = AMF::RegisterPage("AMF Example", "Settings", &DrawSettings);
		const bool b = AMF::RegisterPage("AMF Example", "Advanced", &DrawAdvanced);
		AMF::SetPageVisible("AMF Example", "Advanced", g_advanced);
		REX::INFO("AMF Example: framework {} (API {}); pages registered: Settings={}, Advanced={}", AMF::Version(), AMF::APIVersion(), a, b);
	}
}

OBSE_PLUGIN_LOAD(const OBSE::LoadInterface* a_obse)
{
	OBSE::Init(a_obse);
	if (auto* messaging = OBSE::GetMessagingInterface()) {
		messaging->RegisterListener(&OnMessage);
	}
	return true;
}
