// ============================================================================================================
// Stop time while the framework window is open (the owner, 2026-09-22 and again 2026-09-29: "the next version of AMF
// to have a toggle for stopping time while it's open") - [Menu] bPauseGame, off by default.
//
// Unreal's own pause: GameplayStatics::SetGamePaused(WorldContextObject, bPaused), the call the game's menus make, so
// the world, actors, weather and timers stop the way they do behind the game's pause menu. Applied from the game
// thread's frame tick on the window's visibility edges. The game's state before the window opened is remembered:
// a window opened over a menu that had already paused the game (the System page, the journal) pauses nothing and
// UNpauses nothing when it closes - only a pause this code set is lifted by it.
// ============================================================================================================
#include "Pause.h"

#include "Renderer.h"
#include "Settings.h"
#include "Ue.h"

namespace pause
{
	namespace
	{
		bool g_wasVisible = false;
		bool g_ours = false;   // the pause in effect is this code's

		UE::UObject* Statics()
		{
			static UE::UObject* cdo = nullptr;
			if (!cdo) {
				auto* cls = ue::Class(L"/Script/Engine.GameplayStatics");
				cdo = cls ? cls->GetDefaultObject(false) : nullptr;
			}
			return cdo;
		}

		UE::UObject* World()
		{
			static UE::UClass* cls = nullptr;
			if (!cls) cls = ue::Class(L"/Script/Engine.GameInstance");
			return cls ? ue::FirstOf(cls) : nullptr;   // the live game instance (VAltarGameInstance), a world context
		}

		bool IsPaused(UE::UObject* a_world)
		{
			ue::Call c(Statics(), L"IsGamePaused");
			if (!c || ue::Dying(a_world)) return false;
			c.Set("WorldContextObject", a_world);
			if (!c.RunGuarded()) return false;
			return c.Get<bool>("ReturnValue");
		}

		bool SetPaused(UE::UObject* a_world, bool a_paused)
		{
			ue::Call c(Statics(), L"SetGamePaused");
			if (!c || ue::Dying(a_world)) return false;
			c.Set("WorldContextObject", a_world);
			c.Set("bPaused", a_paused);
			if (!c.RunGuarded()) return false;
			return c.Get<bool>("ReturnValue");
		}
	}

	namespace
	{
		// ---- the idle vanity camera, kept off while the window is open ([Menu] bKeepCameraAwake) ----
		reflect::Handle g_cameraManager;   // handles, not raw pointers: both are rebuilt on every level load
		reflect::Handle g_controller;
		ULONGLONG    g_vanityAt = 0;
		bool         g_vanityHeld = false;   // the timer is stopped by this code (restarted on close)

		UE::UObject* Live(reflect::Handle& a_cache, const wchar_t* a_class)
		{
			if (auto* o = a_cache.Get()) return o;
			auto* cls = ue::Class(a_class);
			a_cache.Set(cls ? ue::FirstOf(cls) : nullptr);
			return a_cache.Get();
		}

		bool VanityCameraUp(UE::UObject* a_cm)
		{
			ue::Call c(a_cm, L"GetCurrentCameraTag");
			if (!c || !c.Run()) return false;
			const auto* name = static_cast<const UE::FName*>(c.At("ReturnValue"));   // FGameplayTag: its FName first
			return name && pe::Utf8(name->ToString()).find("Vanity") != std::string::npos;
		}

		void Vanity(bool a_visible, bool a_wasVisible)
		{
			if (!settings::Get().keepCameraAwake) {
				if (g_vanityHeld) { a_visible = false; }   // the switch went off: let the timer go
				else return;
			}
			const ULONGLONG now = GetTickCount64();
			if (a_visible) {
				if (!a_wasVisible || now - g_vanityAt >= 1000) {   // on open, then once a second (the game may start it again)
					g_vanityAt = now;
					if (auto* cm = Live(g_cameraManager, L"/Script/Altar.VAltarPlayerCameraManager")) {
						ue::Call stop(cm, L"StopVanityCameraTimer");
						if (stop && stop.Run() && !g_vanityHeld) {
							g_vanityHeld = true;
							logger::info("camera: the idle vanity camera's timer is stopped while the window is open");
						}
						if (VanityCameraUp(cm)) {
							if (auto* pc = Live(g_controller, L"/Script/Altar.VAltarPlayerController")) {
								ue::Call leave(pc, L"ExitVanityCamera");
								if (leave && leave.Run()) logger::info("camera: the vanity camera was up - left it, the HUD is back");
							}
						}
					}
				}
			} else if (g_vanityHeld) {
				g_vanityHeld = false;
				if (auto* cm = Live(g_cameraManager, L"/Script/Altar.VAltarPlayerCameraManager")) {
					ue::Call restart(cm, L"RestartFromPauseVanityCameraTimer");   // what the game does after its own pause menu
					if (restart && restart.Run()) logger::info("camera: the vanity camera's timer runs again");
				}
			}
		}
	}

	void Tick()
	{
		const bool visible = renderer::IsMainWindowVisible();
		Vanity(visible, g_wasVisible);
		// 1.0.4 ships without the pause: GameplayStatics::SetGamePaused took the call and the world kept running (the owner,
		// 2026-09-29: "finalize AMF in its current form without the time stop feature for now because it doesn't currently
		// work"). The setting is read and kept; it does nothing until the pause is wired for real.
		constexpr bool kPauseWired = false;
		const bool want = kPauseWired && settings::Get().pauseGameWhileOpen;
		if (visible && !g_wasVisible && want) {
			if (auto* world = World()) {
				if (IsPaused(world)) {
					logger::info("pause: the game was already paused when the window opened (a game menu) - left as it is");
				} else if (SetPaused(world, true)) {
					g_ours = true;
					logger::info("pause: time stopped while the window is open");
				} else {
					logger::warn("pause: SetGamePaused refused - time keeps running");
				}
			} else {
				logger::warn("pause: no game instance yet - time keeps running");
			}
		}
		if (g_ours && (!visible || !want)) {
			if (auto* world = World(); world && SetPaused(world, false)) {
				logger::info("pause: time runs again ({})", visible ? "the switch was turned off" : "the window closed");
			} else {
				logger::warn("pause: SetGamePaused(false) refused - the game may stay paused");
			}
			g_ours = false;
		}
		g_wasVisible = visible;
	}
}
