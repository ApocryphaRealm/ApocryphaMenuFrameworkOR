#include "Watchdog.h"

#include "Renderer.h"
#include "Settings.h"
#include "Logger.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>

#include <atomic>
#include <chrono>
#include <thread>

namespace watchdog
{
	namespace
	{
		using ExitProcess_t = void(WINAPI*)(UINT);
		ExitProcess_t g_rtlExitUserProcess = nullptr;   // ntdll's implementation: what kernel32!ExitProcess itself calls
		bool g_fastExitInstalled = false;
		std::uint8_t g_savedBytes[5]{};
		std::uint8_t* g_hookTarget = nullptr;

		void WINAPI ExitProcess_Fast(UINT a_code)
		{
			if (!settings::Get().fastExit)
			{
				// The first five bytes of kernel32!ExitProcess are ours now, so the ordinary path is
				// what ExitProcess itself would have called next: ntdll's RtlExitUserProcess.
				logger::info("fast exit is off; letting the game exit the ordinary way (code {})", a_code);
				logger::flush();
				if (g_rtlExitUserProcess) { g_rtlExitUserProcess(a_code); }
				::TerminateProcess(::GetCurrentProcess(), a_code);
			}
			logger::info("fast exit: the game asked to exit (code {}) - terminating the process now, skipping every DLL's and driver's shutdown", a_code);
			logger::flush();
			::TerminateProcess(::GetCurrentProcess(), a_code);
			for (;;) { ::Sleep(1000); }
		}

		// An executable page within +/-2 GB of a_near, so a five-byte relative jump can reach it.
		// Walked outward from a_near in 64 KB steps, both directions, first free region wins.
		std::uint8_t* AllocNear(const std::uint8_t* a_near)
		{
			const auto base = reinterpret_cast<std::uintptr_t>(a_near) & ~static_cast<std::uintptr_t>(0xFFFF);
			constexpr std::uintptr_t kStep = 0x10000;
			constexpr std::uintptr_t kRange = 0x7FF00000;
			for (std::uintptr_t off = kStep; off < kRange; off += kStep)
			{
				for (const std::uintptr_t cand : { base - off, base + off })
				{
					if (cand < 0x10000) { continue; }
					MEMORY_BASIC_INFORMATION mbi{};
					if (!::VirtualQuery(reinterpret_cast<void*>(cand), &mbi, sizeof(mbi))) { continue; }
					if (mbi.State != MEM_FREE) { continue; }
					auto* p = static_cast<std::uint8_t*>(::VirtualAlloc(reinterpret_cast<void*>(cand), 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
					if (p) { return p; }
				}
			}
			return nullptr;
		}

		// Hooks kernel32!ExitProcess itself - NOT an import table. The game executable is Steam-
		// packed: the import directory its in-memory headers point at is the packer's, with no
		// ExitProcess in it (measured 2026-09-05), so the one place every exit passes through is
		// the function. Five bytes at its start become a relative jump to a nearby page holding
		// an absolute jump into ExitProcess_Fast. Refused (never stacked) if those bytes already
		// look like somebody else's jump - another fast-exit mod, say - and reported as such.
		bool HookKernel32ExitProcess(std::string& a_why)
		{
			const auto k32 = ::GetModuleHandleW(L"kernel32.dll");
			const auto ntdll = ::GetModuleHandleW(L"ntdll.dll");
			if (!k32 || !ntdll) { a_why = "kernel32 or ntdll handle missing"; return false; }
			auto* target = reinterpret_cast<std::uint8_t*>(::GetProcAddress(k32, "ExitProcess"));
			g_rtlExitUserProcess = reinterpret_cast<ExitProcess_t>(::GetProcAddress(ntdll, "RtlExitUserProcess"));
			if (!target || !g_rtlExitUserProcess) { a_why = "ExitProcess or RtlExitUserProcess not found"; return false; }
			if (target[0] == 0xE9 || target[0] == 0xEB || (target[0] == 0xFF && target[1] == 0x25))
			{
				a_why = "kernel32!ExitProcess already starts with a jump - another mod hooks it; not stacking ours";
				return false;
			}
			auto* page = AllocNear(target);
			if (!page) { a_why = "no free executable page within reach of kernel32"; return false; }
			// absolute jump: FF 25 00 00 00 00 <8-byte address>
			const auto handler = reinterpret_cast<std::uintptr_t>(&ExitProcess_Fast);
			page[0] = 0xFF; page[1] = 0x25; page[2] = 0; page[3] = 0; page[4] = 0; page[5] = 0;
			std::memcpy(page + 6, &handler, sizeof(handler));
			const auto rel = static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(page)) - static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(target) + 5);
			if (rel > INT32_MAX || rel < INT32_MIN) { a_why = "near page is not within a relative jump after all"; return false; }
			DWORD previous = 0;
			if (!::VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &previous)) { a_why = "VirtualProtect on kernel32!ExitProcess refused"; return false; }
			std::memcpy(g_savedBytes, target, 5);
			const auto rel32 = static_cast<std::int32_t>(rel);
			target[0] = 0xE9;
			std::memcpy(target + 1, &rel32, 4);
			DWORD ignored = 0;
			::VirtualProtect(target, 5, previous, &ignored);
			::FlushInstructionCache(::GetCurrentProcess(), target, 5);
			g_hookTarget = target;
			a_why = "kernel32!ExitProcess now jumps to the framework, which ends the process outright";
			return true;
		}
		using clock = std::chrono::steady_clock;

		std::atomic<std::uint64_t> g_frames{ 0 };
		std::atomic<std::int64_t>  g_lastFrameMs{ 0 };   // steady-clock ms at the last Tick()
		std::atomic<bool>          g_started{ false };

		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count();
		}

		std::int64_t SinceLastFrameMs()
		{
			const std::int64_t last = g_lastFrameMs.load(std::memory_order_relaxed);
			return last == 0 ? 0 : (NowMs() - last);
		}

		void Monitor()
		{
			// Deliberately coarse: a poll every 2s is plenty for a window measured in minutes, and
			// keeps this thread invisible in a profile.
			for (;;)
			{
				std::this_thread::sleep_for(std::chrono::seconds(2));

				if (!settings::Get().watchdogEnabled)
				{
					continue;
				}

				const std::int64_t windowMs = static_cast<std::int64_t>(settings::Get().watchdogSeconds) * 1000;
				if (windowMs <= 0)
				{
					continue;
				}

				// Only meaningful once the renderer has produced at least one frame - before that
				// the game is still starting up and a "stall" is expected.
				if (g_lastFrameMs.load(std::memory_order_relaxed) == 0)
				{
					continue;
				}

				// A game that is not in the foreground, or is minimised, presents no frames by design -
				// Skyrim pauses its render loop when its window loses focus (unless a display mod forces
				// background rendering). That is not a hang. Twice on 2026-09-18 the owner alt-tabbed away
				// from a healthy game to type and this watchdog terminated it after 120s, with no crash log
				// to say so. While the window is not the foreground window the clock is held at 'now',
				// so the window has the full 120s again once focus returns.
				{
					const HWND game = static_cast<HWND>(renderer::GetGameWindow());
					const bool inForeground = game && ::GetForegroundWindow() == game && !::IsIconic(game);
					static bool s_wasForeground = true;
					if (!inForeground)
					{
						if (s_wasForeground) { logger::info("watchdog: standing down - the game window is not in the foreground, so no frames are expected"); }
						s_wasForeground = false;
						g_lastFrameMs.store(NowMs(), std::memory_order_relaxed);
						continue;
					}
					if (!s_wasForeground) { logger::info("watchdog: the game window is in the foreground again; the {}s window restarts now", windowMs / 1000); s_wasForeground = true; }
				}

				const std::int64_t stalled = SinceLastFrameMs();
				if (stalled >= windowMs)
				{
					KillNow(std::string("watchdog: no frame for ") + std::to_string(stalled / 1000) +
							"s (window " + std::to_string(windowMs / 1000) + "s) - treating the game as hung");
				}
			}
		}
	}

	void Init()
	{
		bool expected = false;
		if (!g_started.compare_exchange_strong(expected, true))
		{
			return;
		}

		std::thread(Monitor).detach();
		logger::info("watchdog: monitor started (enabled={}, window={}s)",
					 settings::Get().watchdogEnabled, settings::Get().watchdogSeconds);
	}

	void Tick()
	{
		g_frames.fetch_add(1, std::memory_order_relaxed);
		g_lastFrameMs.store(NowMs(), std::memory_order_relaxed);
	}

	void KillNow(const std::string& a_reason)
	{
		// Log and FLUSH before terminating - TerminateProcess runs no cleanup, so an unflushed
		// sink would lose exactly the line explaining why the game vanished.
		logger::critical("watchdog: terminating the process - {}", a_reason);
		logger::flush();

		// The one call that reliably ends a game whose main thread is wedged: issued from a thread
		// that is still running, inside the process itself. External kills cannot help there.
		::TerminateProcess(::GetCurrentProcess(), 0xA3F0DEAD);

		// TerminateProcess does not return for the calling process, but the compiler wants an
		// escape for [[noreturn]].
		for (;;) { std::this_thread::sleep_for(std::chrono::seconds(1)); }
	}

	bool InstallFastExit(std::string& a_reason)
	{
		if (g_fastExitInstalled) { a_reason = "already installed"; return true; }
		if (HookKernel32ExitProcess(a_reason))
		{
			g_fastExitInstalled = true;
			logger::info("fast exit: {} (enabled={})", a_reason, settings::Get().fastExit);
			return true;
		}
		logger::warn("fast exit NOT installed: {} - the ordinary exit path stays", a_reason);
		return false;
	}

	bool FastExitInstalled() { return g_fastExitInstalled; }

	std::string StatusJson()
	{
		const std::int64_t stalledMs = SinceLastFrameMs();
		const std::int64_t windowMs = static_cast<std::int64_t>(settings::Get().watchdogSeconds) * 1000;
		const bool hung = settings::Get().watchdogEnabled && windowMs > 0 &&
						  g_lastFrameMs.load(std::memory_order_relaxed) != 0 && stalledMs >= windowMs;

		return std::string("{\"frames\":") + std::to_string(g_frames.load(std::memory_order_relaxed)) +
			   ",\"secondsSinceFrame\":" + std::to_string(stalledMs / 1000) +
			   ",\"watchdogEnabled\":" + (settings::Get().watchdogEnabled ? "true" : "false") +
			   ",\"windowSeconds\":" + std::to_string(settings::Get().watchdogSeconds) +
			   ",\"consideredHung\":" + (hung ? "true" : "false") +
			   ",\"fastExit\":" + (settings::Get().fastExit ? "true" : "false") +
			   ",\"fastExitInstalled\":" + (g_fastExitInstalled ? "true" : "false") + "}";
	}
}
