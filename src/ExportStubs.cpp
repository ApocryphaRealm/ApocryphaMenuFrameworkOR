#include "ExportStubs.h"

#include "Logger.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	std::mutex g_lock;
	std::unordered_map<std::string, void*> g_stubs;   // name -> generated thunk
	std::vector<std::string> g_names;                  // index -> name, for the handler
	std::atomic<std::size_t> g_known{ 0 };
	std::atomic<std::size_t> g_missing{ 0 };

	// One executable page holds every thunk; 29 bytes each, so a page is ~140 stubs, far more
	// than any consumer set will ever ask for. A second page is allocated if that is wrong.
	std::uint8_t* g_page = nullptr;
	std::size_t g_pageUsed = 0;
	constexpr std::size_t kPageSize = 4096;
	constexpr std::size_t kThunkSize = 29;

	// Called by every thunk with its name index. Logs once per name - the "listener" half -
	// and returns 0.0 so XMM0 is zero for a float-returning caller; the thunk zeroes RAX itself.
	extern "C" double __cdecl StubHandler(int a_index)
	{
		static std::mutex once;
		static std::vector<bool> logged;
		std::scoped_lock lock(once);
		if (a_index >= 0 && static_cast<std::size_t>(a_index) >= logged.size()) {
			logged.resize(a_index + 1, false);
		}
		if (a_index >= 0 && !logged[a_index]) {
			logged[a_index] = true;
			std::string name;
			{
				std::scoped_lock l2(g_lock);
				if (static_cast<std::size_t>(a_index) < g_names.size()) {
					name = g_names[a_index];
				}
			}
			logger::warn("export stub CALLED: a consumer invoked \"{}\", which this framework does not export - returned zero instead of crashing. Add this export.", name);
		}
		return 0.0;
	}

	void* BuildThunk(int a_index)
	{
		if (!g_page || g_pageUsed + kThunkSize > kPageSize) {
			g_page = static_cast<std::uint8_t*>(::VirtualAlloc(nullptr, kPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
			g_pageUsed = 0;
			if (!g_page) {
				return nullptr;
			}
		}

		std::uint8_t* p = g_page + g_pageUsed;
		std::uint8_t* w = p;
		const auto handler = reinterpret_cast<std::uint64_t>(&StubHandler);

		// sub rsp, 0x28          - shadow space + keep the call 16-byte aligned
		*w++ = 0x48; *w++ = 0x83; *w++ = 0xEC; *w++ = 0x28;
		// mov ecx, imm32          - the name index
		*w++ = 0xB9; std::memcpy(w, &a_index, 4); w += 4;
		// mov rax, imm64          - StubHandler
		*w++ = 0x48; *w++ = 0xB8; std::memcpy(w, &handler, 8); w += 8;
		// call rax                - XMM0 = 0.0 on return
		*w++ = 0xFF; *w++ = 0xD0;
		// add rsp, 0x28
		*w++ = 0x48; *w++ = 0x83; *w++ = 0xC4; *w++ = 0x28;
		// xor eax, eax            - RAX = 0 (bool/int/pointer callers)
		*w++ = 0x31; *w++ = 0xC0;
		// ret
		*w++ = 0xC3;

		g_pageUsed += kThunkSize;
		::FlushInstructionCache(::GetCurrentProcess(), p, kThunkSize);
		return p;
	}
}

namespace export_stubs
{
	void* StubFor(const char* a_name)
	{
		const std::string name = a_name ? a_name : "";
		std::scoped_lock lock(g_lock);
		if (const auto it = g_stubs.find(name); it != g_stubs.end()) {
			return it->second;
		}
		const auto index = static_cast<int>(g_names.size());
		g_names.push_back(name);
		void* thunk = BuildThunk(index);
		if (!thunk) {
			// Could not make executable memory - the one case a null goes back. Logged loudly
			// because the caller will crash on it exactly as before this file existed.
			logger::error("export stub: could not allocate a thunk for \"{}\" - the consumer will receive null", name);
			g_names.pop_back();
			return nullptr;
		}
		g_stubs[name] = thunk;
		g_missing.fetch_add(1, std::memory_order_relaxed);
		logger::warn("export listener: \"{}\" requested by a consumer and NOT exported - a safe stub was handed back", name);
		return thunk;
	}

	std::size_t KnownRequested() { return g_known.load(std::memory_order_relaxed); }
	std::size_t MissingRequested() { return g_missing.load(std::memory_order_relaxed); }
}

namespace export_stubs::detail
{
	// Called by the GetProcAddress redirect for every name a consumer resolves from our module
	// that we DO export, so the inventory covers both halves.
	void NoteKnown(const char* a_name)
	{
		static std::mutex once;
		static std::vector<std::string> seen;
		std::scoped_lock lock(once);
		const std::string name = a_name ? a_name : "";
		for (const auto& s : seen) {
			if (s == name) {
				return;
			}
		}
		seen.push_back(name);
		g_known.fetch_add(1, std::memory_order_relaxed);
		logger::debug("export listener: \"{}\" resolved by a consumer", name);
	}
}
