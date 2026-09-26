#pragma once

#include <cstdint>
#include <string>

// ============================================================================================
// HANG WATCHDOG + forced exit (the author, 2026-08-28: "there should be a way that it auto closes under
// hung conditions, or you have a control hook somewhere that can close the game, even if it is
// hung" - so testing never depends on him reaching for Task Manager).
//
// WHY AN IN-PROCESS DESIGN. This project already learned that a wedged SkyrimSE.exe survives
// taskkill /F, Stop-Process -Force and even an elevated Task Manager (LOGIC-LIBRARY: the crashed-
// but-not-reaped state). Those all ask the OS to tear down a process whose main thread is stuck in
// a kernel/driver wait. What DOES still work is a thread INSIDE the process calling
// TerminateProcess(GetCurrentProcess(), ...): our watchdog thread keeps running while the main
// thread is hung, so it can end the process from the inside.
//
// Two ways in:
//   * automatic - the render loop calls Tick() every frame; if the frame counter stops advancing
//     for longer than the configured window, the watchdog declares a hang and terminates.
//   * on demand - the `amf.process` DevBench tool (op "kill"), which runs on devbench's listener
//     thread and therefore answers even while the main thread is wedged.
// ============================================================================================

namespace watchdog
{
	// Starts the monitor thread. Safe to call once; later calls are ignored.
	void Init();

	// Called from the present hook every rendered frame - the liveness signal.
	void Tick();

	// Force-exits the process NOW (flushing the log first). a_reason is logged. Callable from any
	// thread, including devbench's listener thread while the main thread is hung.
	[[noreturn]] void KillNow(const std::string& a_reason);

	// FAST EXIT (1.6.1). Hooks kernel32!ExitProcess itself (the Steam-packed executable exposes no import) so the moment the
	// game asks Windows to exit, the process is terminated outright: the log is flushed and nothing
	// else runs. What is skipped is DLL_PROCESS_DETACH for every loaded module and the drivers'
	// teardown - the phase in which a Skyrim 1.7.104 test game wedged on 2026-09-05 into one
	// thread, no window, unreachable by TerminateProcess from outside AND by this watchdog (its
	// thread had already been torn down by then). Returns false when the exe's import could not be
	// found; a_reason names what happened either way.
	bool InstallFastExit(std::string& a_reason);
	bool FastExitInstalled();

	// JSON for the DevBench tool: frame counter, seconds since the last frame, whether the
	// watchdog considers the game hung, and the configured window.
	std::string StatusJson();
}
