#pragma once

// ============================================================================================
// DevBench driving tool (rule 31): exposes the framework window over DevBench's REST/MCP so it
// can be opened, navigated, activated and inspected HEADLESSLY - no human keypress needed - for
// automated testing. Vendors DevBenchAPI.h/.cpp (MIT), independent of devbench's own GPL.
//
// Once registered, drive it with:
//   POST http://127.0.0.1:8920/api/tool/amf.menu   {"op":"open"}
//   {"op":"select","node":"system/quit"}   {"op":"activate"}   {"op":"state"}
// node paths: game-settings | stats | quest | general | system/save | system/load |
//             system/savequit | system/quit | mod:<index>
// ============================================================================================

namespace devbenchtool
{
	// Registers the "amf.menu" tool if DevBench is present. Idempotent; call at kPostLoad and
	// again (a_lastAttempt = true) at kDataLoaded - devbench's server can still be starting at
	// kPostLoad (the rule-17 retry the other mods use).
	void Init(bool a_lastAttempt);
}
