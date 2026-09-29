#pragma once

// ============================================================================================================
// A per-frame callback on the GAME's main thread, by chaining the executable's import slot of USER32!PeekMessageW
// (Unreal's message pump calls it every frame, controller or no controller). The previous target is kept and called,
// so other plugins chaining the same slot work in either order. OBSE64 has no task or main-loop interface.
//
// Brought over from Tween Menu (Oblivion Remastered) (Tick.cpp there), where it was found 2026-09-29 that a tick
// taken from XInputGetState never runs for a keyboard player: the game stops polling a sleeping pad. The controller
// gate stays in Input.cpp; this file only ticks. Engine objects are only ever touched from this callback (logic
// library 7662).
// ============================================================================================================

namespace tick
{
	using FrameCallback = void (*)();
	bool          Install(FrameCallback a_frame);   // at plugin load, on the game's main thread
	std::uint64_t Frames();
}
