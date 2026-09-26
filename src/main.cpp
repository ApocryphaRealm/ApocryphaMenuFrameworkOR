// Apocrypha Menu Framework - Oblivion Remastered (OBSE64). Copyright (C) 2026 ApocryphaRealm. GPL-3.0-or-later.

#include "Overlay.h"

OBSE_PLUGIN_LOAD(const OBSE::LoadInterface* a_obse)
{
	OBSE::Init(a_obse);
	REX::INFO("Apocrypha Menu Framework {} loading (Oblivion Remastered)", "0.1.0");

	// OBSE64 runs Load before the game's WinMain, so the renderer does not exist yet: hook the DXGI factory
	// exports now and pick up the swap chain and its command queue when the game creates them.
	if (!Overlay::Install()) {
		REX::ERROR("the overlay hooks could not be installed; AMF will not draw");
	}
	return true;
}
