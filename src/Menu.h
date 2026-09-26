#pragma once

// What AMF draws. M1: the framework window in the Untarnished theme (solid black, warm off-white #F5F2E9 lines and
// text - Skyrim AMF's original identity) with a few live controls to prove input reaches it. M2 replaces this with
// the ported AMF core (registry, themes, pages, the C API).

namespace Menu
{
	void ApplyStyle();
	void Draw();
	UINT ToggleKey();   // virtual-key code; F1 as in Skyrim AMF
}
