#include "Tick.h"

namespace tick
{
	namespace
	{
		using PeekMessageW_t = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);

		PeekMessageW_t             g_prevPeek = nullptr;
		std::atomic<FrameCallback> g_frame{ nullptr };
		DWORD                      g_gameThread = 0;
		std::atomic<std::uint64_t> g_frames{ 0 };
		ULONGLONG                  g_lastFrame = 0;
		bool                       g_inFrame = false;

		BOOL WINAPI ChainedPeek(LPMSG a_msg, HWND a_wnd, UINT a_min, UINT a_max, UINT a_remove)
		{
			const BOOL r = g_prevPeek ? g_prevPeek(a_msg, a_wnd, a_min, a_max, a_remove) : FALSE;
			if (GetCurrentThreadId() == g_gameThread && !g_inFrame) {
				const ULONGLONG now = GetTickCount64();
				if (now - g_lastFrame >= 2) {   // the pump calls it once per queued message: at most every 2 ms
					g_lastFrame = now;
					g_inFrame = true;   // a callback that pumps messages itself must not re-enter
					g_frames.fetch_add(1, std::memory_order_relaxed);
					if (auto cb = g_frame.load(std::memory_order_acquire)) {
						cb();
					}
					g_inFrame = false;
				}
			}
			return r;
		}

		// Repoints the game executable's import of a_dll!a_name at a_replacement; the old target goes to *a_previous.
		bool Chain(const char* a_dll, const char* a_name, void* a_replacement, void** a_previous)
		{
			auto*       base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
			const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (!dir.VirtualAddress) {
				return false;
			}
			for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc) {
				const char* dll = reinterpret_cast<const char*>(base + desc->Name);
				if (_strnicmp(dll, a_dll, std::strlen(a_dll)) != 0) {
					continue;
				}
				auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
				auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
				for (; names->u1.AddressOfData; ++names, ++slots) {
					if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal) ||
						std::strcmp(reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name, a_name) != 0) {
						continue;
					}
					auto* slot = reinterpret_cast<void**>(&slots->u1.Function);
					DWORD old = 0;
					if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
						return false;
					}
					*a_previous = *slot;
					*slot = a_replacement;
					VirtualProtect(slot, sizeof(void*), old, &old);
					logger::info("tick: chained after the game's {} {} import (previously {})", dll, a_name, *a_previous);
					return true;
				}
			}
			return false;
		}
	}

	bool Install(FrameCallback a_frame)
	{
		g_frame.store(a_frame, std::memory_order_release);
		if (g_prevPeek) {
			return true;
		}
		g_gameThread = GetCurrentThreadId();
		const bool ok = Chain("user32", "PeekMessageW", reinterpret_cast<void*>(&ChainedPeek), reinterpret_cast<void**>(&g_prevPeek));
		if (!ok) {
			logger::error("tick: the game imports no PeekMessageW - no frame tick, the System-menu row cannot be added");
		}
		return ok;
	}

	std::uint64_t Frames()
	{
		return g_frames.load(std::memory_order_relaxed);
	}
}
