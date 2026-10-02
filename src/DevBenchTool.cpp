#include "DevBenchTool.h"
#include "SystemRow.h"

#include "TestBenchAPI.h"
#include "Input.h"
#include "Renderer.h"
#include <imgui.h>
#include "Theme.h"
#include "Settings.h"
#include "Skin.h"
#include "Strings.h"
#include <ctime>
#include <filesystem>
#include "Watchdog.h"
#include "Logger.h"

#include <iterator>
#include <string>

// Exported from main.cpp (the consumer-header surface DEM resolves via GetProcAddress); called
// in-process here for the keybind widget's reserved-key verdict.
extern "C" std::uint32_t SMF_GetReservedKeyCodes(std::int32_t* a_buffer, std::uint32_t a_capacity);

namespace devbenchtool
{
	namespace
	{
		// Minimal extractor for a top-level JSON number field: "key":123 or "key":12.5.
		double JsonNum(const std::string& a_json, const char* a_key, double a_default)
		{
			const std::string needle = std::string("\"") + a_key + "\"";
			auto pos = a_json.find(needle);
			if (pos == std::string::npos) return a_default;
			pos = a_json.find(':', pos + needle.size());
			if (pos == std::string::npos) return a_default;
			++pos;
			while (pos < a_json.size() && (a_json[pos] == ' ' || a_json[pos] == '	')) ++pos;
			try { return std::stod(a_json.substr(pos)); } catch (...) { return a_default; }
		}

		// Minimal extractor for a top-level JSON string field: finds "key":"value" and returns
		// value (handling backslash escapes). Enough for this tool's small, controlled argument
		// shape - no dependency on a JSON library.
		std::string JsonStr(const std::string& a_json, const char* a_key)
		{
			// A KEY is the quoted name followed by a colon. Finding the quoted name alone matched a VALUE first: in
			// {"op": "separator", "separator": "::sep:2"} the lookup of "separator" hit op's value and returned the
			// next field's ("collapse"), so amf.menu op separator collapse / send failed (2026-10-02).
			const std::string key = std::string("\"") + a_key + "\"";
			std::string::size_type pos = 0;
			for (;;)
			{
				pos = a_json.find(key, pos);
				if (pos == std::string::npos)
				{
					return "";
				}
				auto after = pos + key.size();
				while (after < a_json.size() && (a_json[after] == ' ' || a_json[after] == '\t')) { ++after; }
				if (after < a_json.size() && a_json[after] == ':')
				{
					pos = after;
					break;
				}
				pos += key.size();
			}
			++pos;
			while (pos < a_json.size() && (a_json[pos] == ' ' || a_json[pos] == '\t'))
			{
				++pos;
			}
			if (pos >= a_json.size() || a_json[pos] != '"')
			{
				return "";
			}
			++pos;
			std::string out;
			while (pos < a_json.size() && a_json[pos] != '"')
			{
				if (a_json[pos] == '\\' && pos + 1 < a_json.size())
				{
					++pos;
				}
				out += a_json[pos];
				++pos;
			}
			return out;
		}

		// Runs on devbench's LISTENER thread. Every renderer:: call used here is thread-safe
		// (atomic visibility, mutex-guarded selection, RunConsoleCommand marshals to the main
		// thread, registry::Snapshot is thread-safe), so no extra marshalling is needed.
		void MenuTool(void*, const char* a_argsJson, void* a_sink, TestBenchAPI::WriteFn a_write)
		{
			const std::string args = a_argsJson ? a_argsJson : "{}";
			const std::string op = JsonStr(args, "op");

			std::string result;
			if (op == "open")
			{
				// 1.8.0: nested=true opens the window the way the SKSE MENUS row does (sized to the journal),
				// so a journal-art fit can be measured with the journal open and nobody pressing the row.
				const bool nested = args.find("\"nested\":true") != std::string::npos;
				if (nested) { renderer::SetSelectedNode("system/mods"); }
				renderer::SetMenuVisible(true, nested);
				result = std::string("{\"ok\":true,\"op\":\"open\",\"nested\":") + (nested ? "true" : "false") + "}";
			}
			else if (op == "close")
			{
				renderer::SetMenuVisible(false);
				result = "{\"ok\":true,\"op\":\"close\"}";
			}
			else if (op == "select")
			{
				const std::string node = JsonStr(args, "node");
				renderer::SetSelectedNode(node);
				result = "{\"ok\":true,\"op\":\"select\",\"node\":\"" + node + "\"}";
			}
			else if (op == "activate")
			{
				renderer::ActivateSelectedNode();
				result = std::string("{\"ok\":true,\"op\":\"activate\",\"selected\":\"") +
						 renderer::GetSelectedNode() + "\"}";
			}
			else if (op == "language")
			{
				// 1.6.4: switch the framework's own text live - args language ("german", "auto"). Reloads the
				// strings, saves the INI override and rebuilds the atlas for the language's glyphs.
				const std::string lang = JsonStr(args, "language");
				strings::SetLanguage(lang);
				std::string avail;
				for (const auto& l : strings::Available()) { avail += (avail.empty() ? "\"" : ",\"") + l + "\""; }
				result = "{\"ok\":true,\"op\":\"language\",\"showing\":\"" + strings::Language() + "\",\"available\":[" + avail + "]}";
			}
			else if (op == "font")
			{
				// 1.8.9: what the last atlas build holds - the atlas is rebuilt on the render thread after a
				// language switch, so read this again a moment after op=language.
				const auto p = renderer::GetFontProbe();
				result = "{\"ok\":true,\"op\":\"font\",\"language\":\"" + p.language + "\",\"gameLanguage\":\"" + p.gameLanguage +
					"\",\"builds\":" + std::to_string(p.builds) + ",\"glyphs\":" + std::to_string(p.glyphs) +
					",\"atlas\":[" + std::to_string(p.atlasWidth) + "," + std::to_string(p.atlasHeight) + "]" +
					",\"hasKana\":" + (p.hasKana ? "true" : "false") + ",\"hasHangul\":" + (p.hasHangul ? "true" : "false") +
					",\"hasHanzi\":" + (p.hasHanzi ? "true" : "false") + ",\"hasCyrillic\":" + (p.hasCyrillic ? "true" : "false") + "}";
			}
			else if (op == "theme")
			{
				const std::string id = JsonStr(args, "id");
				const bool ok = renderer::SetTheme(id);
				result = std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"op\":\"theme\",\"id\":\"" + id + "\"}";
			}
			else if (op == "alias")
			{
				// Menu-shell personalization: rename a mod's entry (empty name clears the alias).
				const std::string mod = JsonStr(args, "mod");
				const std::string name = JsonStr(args, "name");
				const bool ok = renderer::SetModAlias(mod, name);
				result = std::string("{\"ok\":") + (ok ? "true" : "false") +
						 ",\"op\":\"alias\",\"mod\":\"" + mod + "\",\"name\":\"" + name + "\"}";
			}
			else if (op == "move")
			{
				// 1-based position; every other entry re-flows around it.
				const std::string mod = JsonStr(args, "mod");
				int position = 0;
				try { position = std::stoi(JsonStr(args, "position")); } catch (...) { position = 0; }
				const bool ok = position > 0 && renderer::MoveModTo(mod, position);
				result = std::string("{\"ok\":") + (ok ? "true" : "false") +
						 ",\"op\":\"move\",\"mod\":\"" + mod + "\",\"position\":" + std::to_string(position) + "}";
			}
			else if (op == "separator")
			{
				// 2026-10-02: separators in the mod list - action add {name, mod: the row it goes above} | remove
				// {separator} | send {mod, separator: "" = out of every group} | collapse {separator} | favourite {separator}
				result = renderer::SeparatorOp(JsonStr(args, "action"), JsonStr(args, "name"), JsonStr(args, "mod"), JsonStr(args, "separator"));
			}
			else if (op == "resetorder")
			{
				renderer::ResetModOrder();
				result = "{\"ok\":true,\"op\":\"resetorder\"}";
			}
			else if (op == "nav")
			{
				// 1.6.7: drive the navigation exactly as the D-pad does - args dir left|right.
				// Inside a mod with several sections this walks the tab bar; left at the first tab
				// (or in a pane with no tabs) goes back to the mod list.
				const std::string dir = JsonStr(args, "dir");
				const bool ok = renderer::QueueNav(dir);
				result = std::string("{\"ok\":") + (ok ? "true" : "false") +
						 ",\"op\":\"nav\",\"dir\":\"" + dir + "\"}";
			}
			else if (op == "focus")
			{
				// Put nav in a pane so a nav case can be set up without pressing anything.
				const std::string pane = JsonStr(args, "pane");
				const bool ok = renderer::FocusPane(pane);
				result = std::string("{\"ok\":") + (ok ? "true" : "false") +
						 ",\"op\":\"focus\",\"pane\":\"" + pane + "\"}";
			}
			else if (op == "cursor")
			{
				// Point-and-click driving (1.5.6): place the software cursor at an absolute
				// display-space position. The OS cursor is not what ImGui reads here.
				const double x = JsonNum(args, "x", -1);
				const double y = JsonNum(args, "y", -1);
				if (x < 0 || y < 0)
				{
					result = "{\"ok\":false,\"error\":\"cursor needs x and y (display pixels)\"}";
				}
				else
				{
					input::SetCursorAbsolute(static_cast<float>(x), static_cast<float>(y));
					result = "{\"ok\":true,\"op\":\"cursor\",\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + "}";
				}
			}
			else if (op == "inject")
			{
				// A REAL engine press spliced ahead of this plugin's hook: device keyboard|gamepad|mouse, code, hold.
				const std::string dev = JsonStr(args, "device");
				const std::uint32_t d = dev == "gamepad" ? 2u : (dev == "mouse" ? 1u : 0u);
				const int code = static_cast<int>(JsonNum(args, "code", 0)); const int hold = static_cast<int>(JsonNum(args, "hold", 4));
				if (code <= 0) { result = "{\"ok\":false,\"error\":\"inject needs a code\"}"; }
				else { input::InjectPress(d, static_cast<std::uint32_t>(code), hold); result = "{\"ok\":true,\"op\":\"inject\",\"code\":" + std::to_string(code) + ",\"hold\":" + std::to_string(hold) + "}"; }
			}
			else if (op == "injectchar")
			{
				// REAL CharEvents (what the engine makes from WM_CHAR), one per dispatch, ahead of the hook.
				const std::string text = JsonStr(args, "text");
				input::InjectText(text);
				result = "{\"ok\":true,\"op\":\"injectchar\",\"chars\":" + std::to_string(text.size()) + "}";
			}
			else if (op == "type")
			{
				// Type text into whatever ImGui item is active (the real record path, from the queue on).
				const std::string text = JsonStr(args, "text");
				input::QueueText(text);
				result = "{\"ok\":true,\"op\":\"type\",\"chars\":" + std::to_string(text.size()) + "}";
			}
			else if (op == "stick")
			{
				// Push a thumbstick and let it go: which 0 left | 1 right, x/y in -1..1 (y > 0 is up), hold in frames.
				const int which = static_cast<int>(JsonNum(args, "which", 1));
				const float x = static_cast<float>(JsonNum(args, "x", 0)), y = static_cast<float>(JsonNum(args, "y", 0));
				const int hold = static_cast<int>(JsonNum(args, "hold", 6));
				input::QueueStick(which, x, y, hold);
				result = "{\"ok\":true,\"op\":\"stick\",\"which\":" + std::to_string(which) + ",\"hold\":" + std::to_string(hold) + "}";
			}
			else if (op == "key")
			{
				// Press one key by DirectInput scan code (Backspace 0x0E, Enter 0x1C, Escape 0x01 ...).
				const int code = static_cast<int>(JsonNum(args, "code", 0));
				if (code <= 0) { result = "{\"ok\":false,\"error\":\"key needs a scan code\"}"; }
				else { input::QueueKey(static_cast<std::uint32_t>(code)); result = "{\"ok\":true,\"op\":\"key\",\"code\":" + std::to_string(code) + "}"; }
			}
			else if (op == "click")
			{
				// Press and release at the current cursor; optional x/y places it first. Down and
				// up land on separate frames - trickling is off in this framework, so same-frame pairs vanish.
				const double x = JsonNum(args, "x", -1);
				const double y = JsonNum(args, "y", -1);
				const int button = static_cast<int>(JsonNum(args, "button", 0));
				if (x >= 0 && y >= 0)
				{
					input::SetCursorAbsolute(static_cast<float>(x), static_cast<float>(y));
				}
				input::QueueMouseClick(static_cast<std::uint32_t>(button));
				result = "{\"ok\":true,\"op\":\"click\",\"button\":" + std::to_string(button) + "}";
			}
			else if (op == "bounds")
			{
				// 1.8.0: measure any clip of the open journal as screen fractions (args path), so a new
				// art replacer's pane is measured rather than guessed at.
				const std::string path = JsonStr(args, "path");
				float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
				const bool ok = systemrow::MeasurePath(path, x, y, w, h);
				std::string esc; for (char c : path) { if (c == '"' || c == '\\') { esc += '\\'; } esc += c; }
				result = std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"op\":\"bounds\",\"path\":\"" + esc +
					"\",\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + ",\"w\":" + std::to_string(w) + ",\"h\":" + std::to_string(h) +
					",\"paneLeft\":" + std::to_string(systemrow::PaneLeft()) + "}";
			}
			else if (op == "systemrow")
			{
				// 1.8.4: the live journal category list - every row in order, and whether our press
				// listener is attached. Our row's index moves under us (SetShowMod splices a Mod Manager
				// row in at index 2), so this is how a "the row does nothing" report is actually checked.
				result = std::string("{\"ok\":true,\"op\":\"systemrow\",\"systemRow\":") + systemrow::ListJson() + "}";
			}
			else if (op == "style")
			{
				// 1.7.7: the style colours the menu is DRAWING with, for the "blue selection" report.
				const ImVec4* c = ImGui::GetStyle().Colors;
				auto hex = [](const ImVec4& v) {
					char b[16];
					std::snprintf(b, sizeof(b), "#%02X%02X%02X%02X", (int)(v.x * 255), (int)(v.y * 255), (int)(v.z * 255), (int)(v.w * 255));
					return std::string(b);
				};
				result = std::string("{\"ok\":true,\"op\":\"style\",\"theme\":\"") + theme::GetActiveTheme().id +
					"\",\"Header\":\"" + hex(c[ImGuiCol_Header]) + "\",\"HeaderHovered\":\"" + hex(c[ImGuiCol_HeaderHovered]) +
					"\",\"HeaderActive\":\"" + hex(c[ImGuiCol_HeaderActive]) + "\",\"TabActive\":\"" + hex(c[ImGuiCol_TabActive]) +
					"\",\"NavHighlight\":\"" + hex(c[ImGuiCol_NavHighlight]) + "\",\"WindowBg\":\"" + hex(c[ImGuiCol_WindowBg]) +
					"\",\"TextSelectedBg\":\"" + hex(c[ImGuiCol_TextSelectedBg]) + "\",\"alpha\":" + std::to_string(ImGui::GetStyle().Alpha) + "}";
			}
			else if (op == "state" || op.empty())
			{
				result = renderer::GetMenuStateJson();
			}
			else
			{
				result = "{\"ok\":false,\"error\":\"unknown op '" + op + "'\"}";
			}

			a_write(a_sink, result.c_str());
		}

		// Process control - deliberately separate from the menu tool. Runs on devbench's LISTENER
		// thread, which keeps answering while the game's main thread is wedged, so "kill" works on a
		// hung game where taskkill/Task Manager do not (see Watchdog.h).
		void ProcessTool(void*, const char* a_argsJson, void* a_sink, TestBenchAPI::WriteFn a_write)
		{
			const std::string args = a_argsJson ? a_argsJson : "{}";
			const std::string op = JsonStr(args, "op");

			if (op == "kill")
			{
				// Answer BEFORE terminating, so the caller gets a reply rather than a dropped socket.
				a_write(a_sink, "{\"ok\":true,\"op\":\"kill\",\"note\":\"terminating now\"}");
				watchdog::KillNow("amf.process kill requested over DevBench");
			}

			// op=skin - what menu art actually loaded, and op=skinreload - load it again from
			// disk without restarting the game, so a UI author can iterate on their PNGs live.
			if (op == "skin")
			{
				a_write(a_sink, (std::string(R"({"ok":true,"op":"skin","skin":)") + skin::StatusJson() + "}").c_str());
				return;
			}
			if (op == "skinreload")
			{
				skin::Reload();
				a_write(a_sink, (std::string(R"({"ok":true,"op":"skinreload","skin":)") + skin::StatusJson() + "}").c_str());
				return;
			}
			if (op == "capture")
			{
				// Saves the current frame WITH the framework overlay to
				// OBSE\\Plugins\\ApocryphaMenuFramework\\captures\\<name>.png (name from args, default a timestamp).
				std::string name = JsonStr(args, "name");
				if (name.empty()) { name = "capture-" + std::to_string(static_cast<long long>(std::time(nullptr))); }
				for (char& c : name) { if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) { c = '_'; } }
				std::filesystem::path dir = std::filesystem::path("OBSE") / "Plugins" / "ApocryphaMenuFramework" / "captures";
				std::error_code ec; std::filesystem::create_directories(dir, ec);
				const std::filesystem::path file = std::filesystem::absolute(dir / (name + ".png"), ec);
				const std::string err = renderer::CaptureBlocking(file.wstring(), 3000);
				std::string esc; for (char c : file.string()) { if (c == '\\' || c == '"') { esc += '\\'; } esc += c; }
				std::string reply = std::string("{\"ok\":") + (err.empty() ? "true" : "false") + ",\"op\":\"capture\",\"path\":\"" + esc + "\"";
				if (!err.empty()) { reply += ",\"error\":\"" + err + "\""; }
				reply += "}";
				a_write(a_sink, reply.c_str());
				return;
			}

			const std::string status = watchdog::StatusJson();
			if (op == "status" || op.empty())
			{
				a_write(a_sink, status.c_str());
				return;
			}
			const std::string err = "{\"ok\":false,\"error\":\"unknown op\",\"status\":" + status + "}";
			a_write(a_sink, err.c_str());
		}

		// Human-readable names for the common DirectInput scan codes the widget reports.
		// Unknown codes fall back to "scan <n>" - the code number is always in the reply too.
		const char* DikName(std::uint32_t a_code)
		{
			switch (a_code)
			{
			case 0x01: return "Escape"; case 0x0F: return "Tab"; case 0x1C: return "Enter";
			case 0x1D: return "Left Ctrl"; case 0x2A: return "Left Shift"; case 0x36: return "Right Shift";
			case 0x38: return "Left Alt"; case 0x39: return "Space"; case 0x3A: return "Caps Lock";
			case 0x0E: return "Backspace"; case 0xC8: return "Up Arrow"; case 0xD0: return "Down Arrow";
			case 0xCB: return "Left Arrow"; case 0xCD: return "Right Arrow";
			case 0x10: return "Q"; case 0x11: return "W"; case 0x12: return "E"; case 0x13: return "R";
			case 0x14: return "T"; case 0x15: return "Y"; case 0x16: return "U"; case 0x17: return "I";
			case 0x18: return "O"; case 0x19: return "P"; case 0x1E: return "A"; case 0x1F: return "S";
			case 0x20: return "D"; case 0x21: return "F"; case 0x22: return "G"; case 0x23: return "H";
			case 0x24: return "J"; case 0x25: return "K"; case 0x26: return "L"; case 0x2C: return "Z";
			case 0x2D: return "X"; case 0x2E: return "C"; case 0x2F: return "V"; case 0x30: return "B";
			case 0x31: return "N"; case 0x32: return "M";
			case 0x02: return "1"; case 0x03: return "2"; case 0x04: return "3"; case 0x05: return "4";
			case 0x06: return "5"; case 0x07: return "6"; case 0x08: return "7"; case 0x09: return "8";
			case 0x0A: return "9"; case 0x0B: return "0";
			case 0x3B: return "F1"; case 0x3C: return "F2"; case 0x3D: return "F3"; case 0x3E: return "F4";
			case 0x3F: return "F5"; case 0x40: return "F6"; case 0x41: return "F7"; case 0x42: return "F8";
			case 0x43: return "F9"; case 0x44: return "F10"; case 0x57: return "F11"; case 0x58: return "F12";
			default: return nullptr;
			}
		}

		// The framework's own reserved-key report, called in-process (same DLL, same export
		// consumers like DEM use through GetProcAddress).
		bool IsReservedKey(std::uint32_t a_code)
		{
			std::int32_t buffer[32]{};
			const auto count = SMF_GetReservedKeyCodes(buffer, static_cast<std::uint32_t>(std::size(buffer)));
			for (std::uint32_t i = 0; i < count && i < std::size(buffer); ++i)
			{
				if (static_cast<std::uint32_t>(buffer[i]) == a_code) { return true; }
			}
			return false;
		}

		// The keybind-capture widget (queue L26): a reusable capture surface for testing binds
		// without going through a mod's own settings page. arm -> the next real (or InputBench-
		// spliced) keyboard/gamepad press is recorded WITHOUT being consumed; state -> what got
		// captured, with a name, the device, and whether the framework reserves that key;
		// rebind -> arms the REAL menu toggle-key rebind (the consuming settings-page path);
		// cancel -> disarms both.
		void KeybindTool(void*, const char* a_argsJson, void* a_sink, TestBenchAPI::WriteFn a_write)
		{
			const std::string args = a_argsJson ? a_argsJson : "{}";
			const std::string op = JsonStr(args, "op");

			if (op == "arm")
			{
				input::ArmKeyCapture();
				a_write(a_sink, "{\"ok\":true,\"op\":\"arm\",\"note\":\"next keyboard/gamepad press is recorded, not consumed\"}");
				return;
			}
			if (op == "rebind")
			{
				input::BeginRebindToggleKey();
				a_write(a_sink, "{\"ok\":true,\"op\":\"rebind\",\"note\":\"next keyboard key becomes the menu toggle key; Escape cancels\"}");
				return;
			}
			if (op == "cancel")
			{
				input::CancelKeyCapture();
				a_write(a_sink, "{\"ok\":true,\"op\":\"cancel\"}");
				return;
			}
			if (op == "state" || op.empty())
			{
				const auto packed = input::LastCapturedKey();
				std::string captured = "null";
				if (packed >= 0)
				{
					const auto device = static_cast<std::uint32_t>(packed >> 32);
					const auto code = static_cast<std::uint32_t>(packed & 0xFFFFFFFF);
					const char* name = device == 0 ? DikName(code) : nullptr;
					const char* deviceName = device == 0 ? "keyboard" : (device == 2 ? "gamepad" : "other");
					const std::string nameStr = name ? std::string{ name } : ("scan " + std::to_string(code));
					captured = "{\"code\":" + std::to_string(code) +
							   ",\"device\":\"" + deviceName + "\"" +
							   ",\"name\":\"" + nameStr + "\"" +
							   ",\"reserved\":" + (device == 0 && IsReservedKey(code) ? "true" : "false") + "}";
				}
				const std::string reply =
					std::string("{\"ok\":true") +
					",\"armed\":" + (input::IsKeyCaptureArmed() ? "true" : "false") +
					",\"rebindArmed\":" + (input::IsAwaitingRebind() ? "true" : "false") +
					",\"toggleKey\":" + std::to_string(settings::Get().toggleKey) +
					",\"captured\":" + captured + "}";
				a_write(a_sink, reply.c_str());
				return;
			}
			a_write(a_sink, "{\"ok\":false,\"error\":\"op must be arm|state|rebind|cancel\"}");
		}
	}

	void Init(bool a_lastAttempt)
	{
		static bool registered = false;
		if (registered)
		{
			return;
		}

		// Oblivion Remastered: the private TestBench plugin serves the same HTTP interface as Skyrim's DevBench
		// (127.0.0.1:8920), and its ITestBenchInterface001 has the same RegisterTool shape.
		TestBenchAPI::ITestBenchInterface001* dev = nullptr;
		if (HMODULE tb = ::GetModuleHandleW(L"TestBench.dll"))
		{
			if (auto get = reinterpret_cast<void* (*)(unsigned)>(::GetProcAddress(tb, "TestBench_GetInterface")))
			{
				dev = static_cast<TestBenchAPI::ITestBenchInterface001*>(get(1));
			}
		}
		if (!dev)
		{
			if (a_lastAttempt)
			{
				logger::info("TestBench not detected; the amf.menu driving tool is unavailable this "
							 "session (the menu still works normally)");
			}
			else
			{
				logger::debug("TestBench not detected yet; will retry at the next message");
			}
			return;
		}

		constexpr const char* descriptor =
			"{"
			"\"description\":\"Drive and inspect the Apocrypha Menu Framework window for testing. "
			"op: open|close|select|activate|state|alias|move|separator (action add|remove|send|collapse|favourite)|resetorder|theme|language|font|nav|focus|cursor|click "
			"(args language: a translation file name or auto). For select, node is a path: settings, controls, help "
			"(pre-1.4.4 system/... paths are still accepted) or mod:<index>. "
			"activate is a no-op in the SMF shape (kept for compatibility). alias renames a mod's menu entry "
			"(args mod, name; empty name clears it); move sends it to a 1-based position (args mod, position) and "
			"re-flows the rest; resetorder returns the list to alphabetical; theme switches the active theme by "
			"registry id (arg id: skyrim, untarnished). nav presses the D-pad (arg dir: left|right) - inside a "
			"mod with several sections that walks its tabs, and left at the first tab returns to the mod list; "
			"focus puts navigation in a pane (arg pane: list|options). state returns visibility, the "
			"selected node, the open mod's page/pageIndex/pageCount, every registered mod + its pages, "
			"the player-facing displayOrder, and - for diagnosing consumer hotkeys - blockingWindowOpen "
			"(exactly what IsAnyBlockingWindowOpened answers a mod) plus consumerWindows, each with its "
			"open/blocking flags and view name, so a window latched open names itself.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{"
			"\"op\":{\"type\":\"string\"},\"node\":{\"type\":\"string\"},"
			"\"mod\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},\"position\":{\"type\":\"string\"},"
			"\"id\":{\"type\":\"string\"},\"dir\":{\"type\":\"string\"},\"pane\":{\"type\":\"string\"}}},"
			"\"readOnly\":false"
			"}";

		if (dev->RegisterTool("amf.menu", descriptor, &MenuTool, nullptr))
		{
			logger::info("Registered \"amf.menu\" driving tool with TestBench (build {})", dev->GetBuildNumber());
		}
		else
		{
			logger::warn("DevBench reported \"amf.menu\" replaced an existing tool of the same name");
		}

		constexpr const char* procDescriptor =
			"{"
			"\"description\":\"Process control for the running game. op: status (frame counter, seconds "
			"since the last rendered frame, whether the hang watchdog considers it hung) | kill (force-exit "
			"the game immediately - works even when the main thread is HUNG and taskkill cannot help, "
			"because it terminates from inside the process).\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\"}}},"
			"\"readOnly\":false"
			"}";
		if (dev->RegisterTool("amf.process", procDescriptor, &ProcessTool, nullptr))
		{
			logger::info("Registered \"amf.process\" (status/kill) with TestBench");
		}

		constexpr const char* keybindDescriptor =
			"{"
			"\"description\":\"Keybind-capture widget for testing binds. op: arm (record the next "
			"keyboard/gamepad press WITHOUT consuming it) | state (armed flags, current toggle key, last "
			"captured key with name/device/reserved verdict) | rebind (arm the real menu toggle-key "
			"rebind; Escape cancels) | cancel.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\"}}},"
			"\"readOnly\":false"
			"}";
		if (dev->RegisterTool("amf.keybind", keybindDescriptor, &KeybindTool, nullptr))
		{
			logger::info("Registered \"amf.keybind\" (capture widget) with TestBench");
		}


		registered = true;
	}
}
