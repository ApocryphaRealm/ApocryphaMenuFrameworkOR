#pragma once

// Stop time while the framework window is open ([Menu] bPauseGame) through Unreal's own SetGamePaused - Pause.cpp.
namespace pause
{
	void Tick();   // the game thread's frame tick
}
