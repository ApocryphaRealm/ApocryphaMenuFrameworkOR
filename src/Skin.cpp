#include "PCH.h"

#include "Skin.h"

#include "ConsumerSurface.h"
#include "Settings.h"
#include "Theme.h"

#include "Logger.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <format>
#include <string>

namespace skin
{
	namespace
	{
		struct Entry
		{
			std::string path;      // as configured, after resolution; empty = not configured
			void*       srv = nullptr;
			ImVec2      size{ 0.0f, 0.0f };
			std::string error;     // why it is not loaded, for StatusJson and the log
		};

		Entry g_frame;
		Entry g_background;
		std::array<Entry, static_cast<std::size_t>(Plate::kCount)> g_plates;
		float g_frameCorner = 64.0f;

		constexpr const char* kPlateFile[] = { "toggle.png", "slider.png", "tab.png" };
		constexpr const char* kPlateName[] = { "toggle", "slider", "tab" };

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}

		// An author writes a path the way they think about their own mod. On Oblivion Remastered that is
		// relative to the game's working folder (Binaries\Win64, where OBSE\Plugins lives) - Skyrim resolved
		// it under Data, and that prefix left the theme art at "Data\OBSE\..." where nothing is (first M2 run,
		// 2026-09-26). An absolute path is accepted too, so a work-in-progress file can be pointed at.
		std::string Resolve(const std::string& a_configured)
		{
			if (a_configured.empty()) { return {}; }
			std::filesystem::path p(a_configured);
			return p.make_preferred().string();
		}

		// Takes an ALREADY-RESOLVED path. It used to call Resolve() itself, which double-prefixed
		// the plate files ("Data\Data\Interface\...") because the plate loop resolves the folder
		// before appending the file name. Resolving happens at the call sites now, exactly once.
		void Load(Entry& a_entry, const std::string& a_path, const char* a_what)
		{
			a_entry = Entry{};
			if (a_path.empty()) { return; }

			a_entry.path = a_path;

			// DDS is the one an author is most likely to reach for, being Skyrim's own texture
			// format, and it is exactly the one that cannot work here. Say so by name.
			if (Lower(std::filesystem::path(a_entry.path).extension().string()) == ".dds")
			{
				a_entry.error = "DDS is not decoded - save it as a 32-bit RGBA PNG";
				logger::warn("skin: {} \"{}\" is a .dds. AMF decodes PNG (WIC), not DDS - export it as a "
							 "32-bit RGBA PNG instead.", a_what, a_entry.path);
				return;
			}

			std::error_code ec;
			if (!std::filesystem::exists(a_entry.path, ec))
			{
				a_entry.error = "file not found";
				logger::warn("skin: {} \"{}\" does not exist", a_what, a_entry.path);
				return;
			}

			ImVec2 size{ 0.0f, 0.0f };
			void* srv = consumer::LoadTexture(a_entry.path.c_str(), &size);
			if (!srv)
			{
				a_entry.error = "could not decode";
				logger::warn("skin: {} \"{}\" could not be decoded - is it a real 32-bit RGBA PNG?",
							 a_what, a_entry.path);
				return;
			}

			a_entry.srv = srv;
			a_entry.size = size;
			logger::info("skin: {} loaded from \"{}\" ({:.0f}x{:.0f})", a_what, a_entry.path, size.x, size.y);
		}
	}

	void Reload()
	{
		const auto& v = settings::Get();

		// WHERE THE ART COMES FROM. The master switch is honoured HERE rather than at each draw site,
		// so exactly one place decides whether custom art exists at all. On, the player's own [Skin]
		// art draws. Off, the ACTIVE THEME's art draws if it has any (1.9.8: a theme picked in the
		// Theme list is itself the request for that theme's look), and otherwise nothing does - which
		// is what a player who installed no art replacer must get, including when paths are still
		// sitting in the INI from somebody's experiment.
		std::string   frame, background, plates;
		std::uint32_t corner = 64;
		const char*   source = "none";
		if (v.skinEnabled)
		{
			frame = v.skinFrame;
			background = v.skinBackground;
			plates = v.skinPlates;
			corner = v.skinFrameCorner;
			source = "[Skin]";
		}
		else if (!theme::ListThemes().empty())   // the registry fills at theme::Apply; D3D init can run first
		{
			const theme::Palette& t = theme::GetActiveTheme();
			frame = t.skinFrame;
			background = t.skinBackground;
			plates = t.skinPlates;
			corner = t.skinFrameCorner;
			source = "theme";
		}

		g_frame = Entry{};
		g_background = Entry{};
		for (std::size_t i = 0; i < g_plates.size(); ++i) { g_plates[i] = Entry{}; }
		if (frame.empty() && background.empty() && plates.empty())
		{
			return;
		}
		logger::info("skin: loading art from {}", source);

		Load(g_frame, Resolve(frame), "frame");
		Load(g_background, Resolve(background), "background");

		// Plates live by fixed name inside one folder, so the author has one path to get right
		// and can supply any subset of the three.
		if (!plates.empty())
		{
			const std::filesystem::path dir(Resolve(plates));
			for (std::size_t i = 0; i < g_plates.size(); ++i)
			{
				const auto file = (dir / kPlateFile[i]).string();
				std::error_code ec;
				if (!std::filesystem::exists(file, ec)) { continue; }   // a missing plate is not an error
				Load(g_plates[i], file, kPlateName[i]);
			}
		}

		// Clamp the corner so two of them always fit inside the frame texture. An artist who
		// types 64 for a 96px image would otherwise get flipped middle slices, which looks like
		// corrupt art rather than a bad number.
		g_frameCorner = static_cast<float>(corner);
		if (g_frame.srv && g_frame.size.x > 0.0f && g_frame.size.y > 0.0f)
		{
			const float maxCorner = std::min(g_frame.size.x, g_frame.size.y) * 0.5f - 1.0f;
			if (g_frameCorner > maxCorner)
			{
				logger::warn("skin: uFrameCorner {:.0f} is too large for a {:.0f}x{:.0f} frame; clamped to {:.0f}",
							 g_frameCorner, g_frame.size.x, g_frame.size.y, maxCorner);
				g_frameCorner = maxCorner;
			}
			if (g_frameCorner < 1.0f) { g_frameCorner = 1.0f; }
		}
	}

	bool   HasFrame() { return g_frame.srv != nullptr; }
	void*  FrameTexture() { return g_frame.srv; }
	ImVec2 FrameSize() { return g_frame.size; }
	float  FrameCorner() { return g_frameCorner; }

	bool   HasBackground() { return g_background.srv != nullptr; }
	void*  BackgroundTexture() { return g_background.srv; }
	ImVec2 BackgroundSize() { return g_background.size; }

	bool BackgroundTiles()
	{
		return g_background.srv && g_background.size.x <= static_cast<float>(kTileThreshold) &&
			   g_background.size.y <= static_cast<float>(kTileThreshold);
	}

	bool HasPlate(Plate a_plate)
	{
		const auto i = static_cast<std::size_t>(a_plate);
		return i < g_plates.size() && g_plates[i].srv != nullptr;
	}

	void* PlateTexture(Plate a_plate)
	{
		const auto i = static_cast<std::size_t>(a_plate);
		return i < g_plates.size() ? g_plates[i].srv : nullptr;
	}

	ImVec2 PlateSize(Plate a_plate)
	{
		const auto i = static_cast<std::size_t>(a_plate);
		return i < g_plates.size() ? g_plates[i].size : ImVec2(0.0f, 0.0f);
	}

	std::string StatusJson()
	{
		// 1.9.8: a resolved path is "Data\SKSE/..." - the backslash went out unescaped and the reply was not valid
		// JSON (found when the new themes first loaded art through this op). Escape \ and " in every string.
		auto esc = [](const std::string& a_s) {
			std::string out;
			out.reserve(a_s.size());
			for (const char c : a_s)
			{
				if (c == '\\' || c == '"') { out += '\\'; }
				out += c;
			}
			return out;
		};
		auto one = [&esc](const char* a_name, const Entry& a_e) {
			return std::format(R"({{"what":"{}","path":"{}","loaded":{},"w":{:.0f},"h":{:.0f},"error":"{}"}})",
							   a_name, esc(a_e.path), a_e.srv ? "true" : "false", a_e.size.x, a_e.size.y, esc(a_e.error));
		};
		std::string plates;
		for (std::size_t i = 0; i < g_plates.size(); ++i)
		{
			if (!plates.empty()) { plates += ","; }
			plates += one(kPlateName[i], g_plates[i]);
		}
		return std::format(R"({{"frame":{},"frameCorner":{:.0f},"background":{},"backgroundTiles":{},"plates":[{}]}})",
						   one("frame", g_frame), g_frameCorner, one("background", g_background),
						   BackgroundTiles() ? "true" : "false", plates);
	}
}
