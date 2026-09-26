#pragma once

// ============================================================================================
// Startup curtain (the owner, 2026-09-15: "the mod that makes the screen black until the main
// menu loads" - then, the same day: "build it into amf with a toggle in the settings page").
//
// It lives here rather than in a plugin of its own because the framework already owns the
// present hook and its surface "draws EVERY frame, whether or not our own menu is up". A second
// plugin would have had to stand up a second D3D hook and race this one for the same frame.
//
// FAILING SAFE IS THE WHOLE DESIGN. A curtain that does not lift is indistinguishable from a
// game that will not start, and the player has no way to argue with it. So it lifts on ANY of:
// the main menu appearing, a hard timeout, or the setting being turned off - and once lifted it
// never comes back for the life of the process.
// ============================================================================================

namespace curtain
{
	// Once per rendered frame, after the frame's own widgets and before ImGui::Render().
	// Cheap and self-disabling: after the curtain has lifted this is an atomic load and a return.
	void Draw();

	// True while the screen is still being covered (fully, or mid-fade).
	bool IsCovering();

	// Drops the curtain now, whatever state it is in, and logs why. Idempotent.
	void Lift(const char* a_reason);
}
