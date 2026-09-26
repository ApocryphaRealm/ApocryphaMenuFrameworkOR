#include "Registry.h"

#include "Logger.h"

#include <mutex>

namespace registry
{
	namespace
	{
		std::mutex g_lock;
		std::vector<Entry> g_entries;
	}

	bool Register(const char* a_modName, const char* a_pageName, AMF_RenderCallback a_render)
	{
		if (!a_modName || !*a_modName || !a_pageName || !*a_pageName || !a_render)
		{
			logger::warn("AMF_RegisterPage refused: mod=\"{}\", page=\"{}\", render={} - every argument must be non-null/non-empty",
						 a_modName ? a_modName : "<null>", a_pageName ? a_pageName : "<null>",
						 static_cast<const void*>(reinterpret_cast<void*>(a_render)));
			return false;
		}

		std::scoped_lock lock(g_lock);

		for (auto& entry : g_entries)
		{
			if (entry.modName == a_modName)
			{
				for (const auto& page : entry.pages)
				{
					if (page.pageName == a_pageName)
					{
						logger::warn("AMF_RegisterPage refused: \"{}\" already has a page \"{}\" (duplicate registration)",
									 a_modName, a_pageName);
						return false;
					}
				}

				entry.pages.push_back({ a_pageName, a_render });
				logger::info("page registered: \"{}\" -> \"{}\" (mod now has {} page(s), rendered as tabs - one menu per mod)",
							 a_modName, a_pageName, entry.pages.size());
				return true;
			}
		}

		g_entries.push_back({ a_modName, { { a_pageName, a_render } } });
		logger::info("first page registered for \"{}\": \"{}\" ({} mod(s) in the registry)",
					 a_modName, a_pageName, g_entries.size());
		return true;
	}

	bool SetPageVisible(const char* a_modName, const char* a_pageName, bool a_visible)
	{
		if (!a_modName || !*a_modName || !a_pageName || !*a_pageName)
		{
			logger::warn("AMF_SetPageVisible refused: mod=\"{}\", page=\"{}\" - both names must be non-empty",
						 a_modName ? a_modName : "<null>", a_pageName ? a_pageName : "<null>");
			return false;
		}

		std::scoped_lock lock(g_lock);
		for (auto& entry : g_entries)
		{
			if (entry.modName != a_modName) { continue; }
			for (auto& page : entry.pages)
			{
				if (page.pageName != a_pageName) { continue; }
				if (page.hidden == !a_visible) { return true; }  // already so; the consumer may call every frame
				page.hidden = !a_visible;
				logger::info("page {}: \"{}\" -> \"{}\"", a_visible ? "shown" : "hidden", a_modName, a_pageName);
				return true;
			}
		}
		logger::warn("AMF_SetPageVisible: \"{}\" has no registered page \"{}\"", a_modName, a_pageName);
		return false;
	}

	std::vector<Entry> Snapshot()
	{
		std::scoped_lock lock(g_lock);
		return g_entries;
	}

	std::size_t Count()
	{
		std::scoped_lock lock(g_lock);
		return g_entries.size();
	}
}
