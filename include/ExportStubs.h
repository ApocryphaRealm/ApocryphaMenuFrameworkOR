#pragma once

// ============================================================================================
// EXPORT STUBS - the null guard for exports this framework does NOT have, plus the listener
// that tells us which ones consumers actually ask for.
//
// The stock SKSE Menu Framework consumer header resolves every widget like this:
//
//     static auto func = GetFunction<...>("igSomething");
//     return func(...);                     // no null check
//
// so when a consumer asks our module for a name we do not export, the null it gets back is
// CALLED the first time its page draws - "execute memory at 0x0", the crash Block Overhaul
// produced on 2026-09-04. The 61-export tranche closed the gap for the consumers installed
// here; it cannot close it for consumers we have never seen.
//
// This closes it in general. AMF already owns the import tables of SKSE plugin modules (the
// module-name alias). Redirecting their GetProcAddress import as well lets AMF answer requests
// aimed at ITS OWN module: a name it exports resolves normally; a name it lacks resolves to a
// generated stub that logs the name once and returns zero (RAX and XMM0 both, so bool/int/
// pointer/float callers all read a harmless nothing). The page draws with a hole in it instead
// of taking the game down, and the log names the export to add next.
//
// Only requests for OUR module are touched; every other GetProcAddress passes straight through.
// ============================================================================================

#include <cstddef>

namespace export_stubs
{
	// Returns a callable stub for a_name, creating it on first use. Never returns null.
	void* StubFor(const char* a_name);

	// The listener's inventory: distinct export names consumers have asked our module for,
	// split into ones we have and ones we lack. Exposed for the DevBench tool.
	std::size_t KnownRequested();
	std::size_t MissingRequested();
}
