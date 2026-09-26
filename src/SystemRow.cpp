// Oblivion Remastered has no System-row entry yet (PLAN.md M3 puts AMF on the pause menu instead of the Skyrim
// journal's System tab). The renderer asks these for the journal panel's rectangle when the window was opened from
// that row; with no row, it never is, and every answer says so.
#include "SystemRow.h"

namespace systemrow
{
	bool        Install() { return false; }
	bool        WasInjected() { return false; }
	const char* FoundPath() { return ""; }
	std::string ListJson() { return "[]"; }
	bool        GetPanelRect(float&, float&, float&, float&) { return false; }
	float       PaneLeft() { return 0.0f; }
	const char* ArtKey() { return ""; }
	bool        MeasurePath(const std::string&, float&, float&, float&, float&) { return false; }
}
