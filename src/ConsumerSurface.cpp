#include "ConsumerSurface.h"

#include "Logger.h"

#include <imgui.h>

#include "Gfx.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	struct WindowEntry
	{
		std::unique_ptr<consumer::WindowInterface> iface;
		consumer::RenderFunction render{ nullptr };
		std::string view;   // AddWindowWithView's name; empty for a plain AddWindow
	};

	struct HudEntry
	{
		std::int64_t id{ 0 };
		consumer::HudCallback callback{ nullptr };
	};

	struct TextureEntry
	{
		void* srv{ nullptr };   // the ImTextureID gfx:: handed back (a DX12 SRV descriptor's GPU handle)
		float width{ 0.0f };
		float height{ 0.0f };
	};

	std::mutex g_lock;
	std::vector<WindowEntry> g_windows;
	std::vector<HudEntry> g_hud;
	std::unordered_map<std::string, TextureEntry> g_textures;
	std::int64_t g_nextHudId = 1;

	// How many fonts WE pushed. Pop() only pops what we are responsible for, so a consumer
	// calling Pop() more often than it pushed cannot unbalance the framework's own stack.
	int g_fontDepth = 0;

	std::mutex g_warnLock;
	std::vector<std::string> g_warnedTextures;   // one warning per path, not per frame
}

namespace consumer
{
	WindowInterface* AddWindow(RenderFunction a_render, const char* a_view)
	{
		if (!a_render) {
			logger::warn("AddWindow refused: null render function");
			return nullptr;
		}

		std::scoped_lock lock(g_lock);

		WindowEntry entry;
		entry.iface = std::make_unique<WindowInterface>();
		entry.render = a_render;
		if (a_view) {
			entry.view = a_view;
		}

		auto* const handed = entry.iface.get();
		g_windows.push_back(std::move(entry));

		logger::info("AddWindow{} (SMF-compat): consumer window registered ({} total)",
					 a_view ? "WithView" : "", g_windows.size());

		return handed;
	}

	std::int64_t RegisterHudElement(HudCallback a_callback)
	{
		if (!a_callback) {
			logger::warn("RegisterHudElement refused: null callback");
			return 0;
		}

		std::scoped_lock lock(g_lock);
		const auto id = g_nextHudId++;
		g_hud.push_back(HudEntry{ id, a_callback });
		logger::info("RegisterHudElement (SMF-compat): HUD element {} registered ({} total)", id, g_hud.size());
		return id;
	}

	void UnregisterHudElement(std::int64_t a_id)
	{
		std::scoped_lock lock(g_lock);
		const auto before = g_hud.size();
		std::erase_if(g_hud, [a_id](const HudEntry& e) { return e.id == a_id; });
		if (g_hud.size() != before) {
			logger::info("UnregisterHudElement (SMF-compat): HUD element {} removed ({} left)", a_id, g_hud.size());
		}
	}

	void DrawWindows()
	{
		// Snapshot under the lock, call outside it: a consumer's render function may register
		// another window, and re-entering this mutex from inside it would deadlock the render
		// thread - which presents as the game freezing on a frame, not as an error.
		std::vector<RenderFunction> due;
		{
			std::scoped_lock lock(g_lock);
			due.reserve(g_windows.size());
			for (const auto& w : g_windows) {
				if (w.render && w.iface->IsOpen.load(std::memory_order_acquire)) {
					due.push_back(w.render);
				}
			}
		}

		for (const auto fn : due) {
			// The consumer's callback opens its own ImGui window - SMF's contract, and the
			// reason nothing is begun for it here.
			fn();
		}
	}

	void DrawHudElements()
	{
		std::vector<HudCallback> due;
		{
			std::scoped_lock lock(g_lock);
			due.reserve(g_hud.size());
			for (const auto& h : g_hud) {
				if (h.callback) {
					due.push_back(h.callback);
				}
			}
		}

		for (const auto fn : due) {
			fn();
		}
	}

	bool AnyBlockingWindowOpen()
	{
		std::scoped_lock lock(g_lock);
		return std::any_of(g_windows.begin(), g_windows.end(), [](const WindowEntry& w) {
			return w.iface->IsOpen.load(std::memory_order_acquire) &&
				   w.iface->BlockUserInput.load(std::memory_order_acquire);
		});
	}

	void PushNamedFont(const char* a_name)
	{
		// AMF builds ONE atlas font (the shell's face, rebuilt on scale change), so there is no
		// second face to switch to yet. Pushing the current font keeps every consumer's
		// Push/Pop pair balanced, which is what actually matters for the frames around it - a
		// mismatched name changes appearance, never correctness.
		ImGui::PushFont(ImGui::GetFont());
		++g_fontDepth;

		if (a_name && *a_name) {
			static std::mutex once;
			static std::vector<std::string> logged;
			std::scoped_lock lock(once);
			const std::string name(a_name);
			if (std::find(logged.begin(), logged.end(), name) == logged.end()) {
				logged.push_back(name);
				logger::debug("PushFont (SMF-compat): \"{}\" requested; AMF ships a single face, so the current font was pushed", name);
			}
		}
	}

	void PushRegular() { PushNamedFont("regular"); }
	void PushSolid() { PushNamedFont("solid"); }
	void PushBrands() { PushNamedFont("brands"); }

	void PopFont()
	{
		if (g_fontDepth > 0) {
			ImGui::PopFont();
			--g_fontDepth;
		}
	}

	void* LoadTexture(const char* a_path, ImVec2* a_outSize)
	{
		if (!a_path || !*a_path) {
			return nullptr;
		}

		const std::string path(a_path);

		{
			std::scoped_lock lock(g_lock);
			if (const auto it = g_textures.find(path); it != g_textures.end()) {
				if (a_outSize) {
					a_outSize->x = it->second.width;
					a_outSize->y = it->second.height;
				}
				return it->second.srv;   // cached: consumers call this every frame
			}
		}

		auto warnOnce = [&](const char* a_why) {
			std::scoped_lock lock(g_warnLock);
			if (std::find(g_warnedTextures.begin(), g_warnedTextures.end(), path) == g_warnedTextures.end()) {
				g_warnedTextures.push_back(path);
				logger::warn("LoadTexture (SMF-compat): \"{}\" not loaded - {}", path, a_why);
			}
		};

		if (!gfx::Ready()) {
			warnOnce("the D3D12 device is not up yet");
			return nullptr;
		}

		// Oblivion Remastered is D3D12-only: decode with WIC to RGBA, then upload through the overlay's own
		// copy queue work (gfx::CreateTextureRGBA) - there is no DirectXTK D3D11 loader to lean on here.
		std::vector<std::uint8_t> pixels;
		int w = 0, h = 0;
		if (!gfx::DecodeImageFile(std::filesystem::path(path).wstring(), pixels, w, h)) {
			warnOnce("could not decode it (WIC)");
			return nullptr;
		}
		void* id = gfx::CreateTextureRGBA(pixels.data(), w, h);
		if (!id) {
			warnOnce("the GPU upload failed (see the lines above)");
			return nullptr;
		}

		TextureEntry entry;
		entry.srv = id;
		entry.width = static_cast<float>(w);
		entry.height = static_cast<float>(h);

		{
			std::scoped_lock lock(g_lock);
			g_textures[path] = entry;
		}

		if (a_outSize) {
			a_outSize->x = entry.width;
			a_outSize->y = entry.height;
		}

		logger::info("LoadTexture (SMF-compat): \"{}\" loaded ({}x{})", path, entry.width, entry.height);
		return entry.srv;
	}

	void DisposeTexture(const char* a_path)
	{
		if (!a_path) {
			return;
		}

		std::scoped_lock lock(g_lock);
		if (const auto it = g_textures.find(a_path); it != g_textures.end()) {
			if (it->second.srv) {
				gfx::ReleaseTexture(it->second.srv);
			}
			g_textures.erase(it);
			logger::debug("DisposeTexture (SMF-compat): \"{}\" released", a_path);
		}
	}

	std::size_t WindowCount()
	{
		std::scoped_lock lock(g_lock);
		return g_windows.size();
	}

	std::vector<WindowState> WindowStates()
	{
		// Copied out under the lock - no reference to g_windows escapes, and nothing here calls
		// AnyBlockingWindowOpen(), which takes the same lock. The caller computes the aggregate
		// from this vector instead, so the two can never nest.
		std::scoped_lock lock(g_lock);

		std::vector<WindowState> states;
		states.reserve(g_windows.size());
		for (const WindowEntry& w : g_windows) {
			WindowState s;
			s.open = w.iface->IsOpen.load(std::memory_order_acquire);
			s.blocking = w.iface->BlockUserInput.load(std::memory_order_acquire);
			s.view = w.view;
			states.push_back(std::move(s));
		}
		return states;
	}

	std::size_t HudElementCount()
	{
		std::scoped_lock lock(g_lock);
		return g_hud.size();
	}

	std::size_t TextureCount()
	{
		std::scoped_lock lock(g_lock);
		return g_textures.size();
	}
}
