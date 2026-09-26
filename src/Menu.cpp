#include "Menu.h"

#include "Overlay.h"

#include <imgui.h>

namespace Menu
{
	namespace
	{
		float g_demoSlider = 0.5f;
		bool  g_demoToggle = true;
		int   g_clicks = 0;
	}

	void ApplyStyle()
	{
		// Untarnished, with AMF's graded tints (Skyrim AMF Theme.cpp): lines 55% separators / 28% hover / 14% fills.
		ImGuiStyle& s = ImGui::GetStyle();
		s.WindowRounding = 0.0f;
		s.FrameRounding = 0.0f;
		s.WindowBorderSize = 1.0f;
		s.FrameBorderSize = 1.0f;
		s.WindowPadding = ImVec2(13.0f, 13.0f);
		const ImVec4 black{ 0.0f, 0.0f, 0.0f, 1.0f };
		const ImVec4 line{ 0.961f, 0.949f, 0.914f, 1.0f };   // #F5F2E9
		auto tint = [](ImVec4 c, float a) { c.w = a; return c; };
		ImVec4* c = s.Colors;
		c[ImGuiCol_WindowBg] = black;
		c[ImGuiCol_ChildBg] = black;
		c[ImGuiCol_PopupBg] = black;
		c[ImGuiCol_TitleBg] = black;
		c[ImGuiCol_TitleBgActive] = black;
		c[ImGuiCol_TitleBgCollapsed] = black;
		c[ImGuiCol_Text] = line;
		c[ImGuiCol_TextDisabled] = ImVec4{ 0.604f, 0.592f, 0.561f, 1.0f };
		c[ImGuiCol_Border] = line;
		c[ImGuiCol_Separator] = tint(line, 0.55f);
		c[ImGuiCol_FrameBg] = black;
		c[ImGuiCol_FrameBgHovered] = tint(line, 0.14f);
		c[ImGuiCol_FrameBgActive] = tint(line, 0.28f);
		c[ImGuiCol_Button] = black;
		c[ImGuiCol_ButtonHovered] = tint(line, 0.14f);
		c[ImGuiCol_ButtonActive] = tint(line, 0.28f);
		c[ImGuiCol_Header] = tint(line, 0.22f);
		c[ImGuiCol_HeaderHovered] = tint(line, 0.42f);
		c[ImGuiCol_HeaderActive] = tint(line, 0.42f);
		c[ImGuiCol_SliderGrab] = line;
		c[ImGuiCol_SliderGrabActive] = line;
		c[ImGuiCol_CheckMark] = line;
		c[ImGuiCol_ScrollbarBg] = black;
		c[ImGuiCol_ScrollbarGrab] = tint(line, 0.55f);
		c[ImGuiCol_NavHighlight] = ImVec4{ 0.24f, 0.62f, 1.00f, 1.00f };   // AMF's bright blue controller nav box
		ImGui::GetIO().FontGlobalScale = 1.4f;
	}

	void Draw()
	{
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(vp->Size.x * 0.5f, vp->Size.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.42f, vp->Size.y * 0.5f), ImGuiCond_FirstUseEver);
		bool open = true;
		if (ImGui::Begin("Apocrypha Menu Framework", &open, ImGuiWindowFlags_NoCollapse)) {
			ImGui::TextUnformatted("Apocrypha Menu Framework - Oblivion Remastered");
			ImGui::Separator();
			ImGui::TextWrapped("This is the first build: the framework window drawn over the game's DirectX 12 frames, "
			                   "with mouse and keyboard taken from the game while it is open. Press F1 to close it.");
			ImGui::Spacing();
			ImGui::Checkbox("A switch", &g_demoToggle);
			ImGui::SliderFloat("A slider", &g_demoSlider, 0.0f, 1.0f);
			if (ImGui::Button("A button")) {
				++g_clicks;
			}
			ImGui::SameLine();
			ImGui::Text("pressed %d time%s", g_clicks, g_clicks == 1 ? "" : "s");
			ImGui::Spacing();
			ImGui::TextDisabled("%.0f fps, %dx%d", ImGui::GetIO().Framerate, static_cast<int>(vp->Size.x), static_cast<int>(vp->Size.y));
		}
		ImGui::End();
		if (!open) {
			Overlay::SetOpen(false);
		}
	}

	UINT ToggleKey() { return VK_F1; }
}
