// Oblivion Remastered has no startup curtain yet: Skyrim's covered the screen until the main menu appeared, keyed to
// RE::UI's MainMenu, and the Oblivion equivalent (the UE front-end widget) is not identified. These keep the
// renderer's calls as no-ops, so the [Startup] setting reads and saves as it does on Skyrim and simply does nothing.
#include "Curtain.h"

namespace curtain
{
	void Draw() {}
	bool IsCovering() { return false; }
	void Lift(const char*) {}
}
