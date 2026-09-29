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
			if (!c || !a_world) return false;
			c.Set("WorldContextObject", a_world);
			c.Run();
			return c.Get<bool>("ReturnValue");
		}

		bool SetPaused(UE::UObject* a_world, bool a_paused)
		{
			ue::Call c(Statics(), L"SetGamePaused");
			if (!c || !a_world) return false;
			c.Set("WorldContextObject", a_world);
			c.Set("bPaused", a_paused);
			c.Run();
			return c.Get<bool>("ReturnValue");
		}
	}

	void Tick()
	{
		const bool visible = renderer::IsMainWindowVisible();
		const bool want = settings::Get().pauseGameWhileOpen;
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
