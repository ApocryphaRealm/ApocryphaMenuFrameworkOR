#pragma once

// ============================================================================================
// M3: the page registry. Mods register named settings pages via the C API; the framework
// window's left pane lists ONE entry per mod, and a mod with several pages renders them as
// TABS inside its single menu - the project's one-menu-multiple-tabs rule implemented at the
// framework level rather than left to each mod.
// ============================================================================================

#include "AMF/API.h"

#include <cstdint>
#include <string>
#include <vector>

namespace registry
{
	struct Page
	{
		std::string pageName;
		AMF_RenderCallback render = nullptr;
		bool hidden = false;  // 1.8.3: AMF_SetPageVisible(false) leaves the page out of the menu until shown again
	};

	struct Entry
	{
		std::string modName;
		std::vector<Page> pages;
	};

	// Thread-safe: registration typically arrives from other plugins' load/messaging threads;
	// iteration happens on the render thread. Returns false only for null/empty arguments or a
	// duplicate (mod, page) pair - both logged.
	bool Register(const char* a_modName, const char* a_pageName, AMF_RenderCallback a_render);

	// 1.8.3: hide or show one registered page (AMF_SetPageVisible). False when the (mod, page) pair is not
	// registered. The page stays registered - hiding only leaves it out of the menu's tabs.
	bool SetPageVisible(const char* a_modName, const char* a_pageName, bool a_visible);

	// Render-thread snapshot access. The copy is cheap at menu scale (a handful of mods) and
	// means the render loop never holds the registration lock across user callbacks.
	std::vector<Entry> Snapshot();

	std::size_t Count();
}
