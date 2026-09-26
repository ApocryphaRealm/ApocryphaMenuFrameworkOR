// SPDX-License-Identifier: MIT
// TestBench cross-plugin API for Oblivion Remastered - the same shape as Skyrim DevBench's IDevBenchInterface001, so a
// plugin written against that API (AMF's amf.menu, Wheeler's tools) registers here the same way. Header-only and
// dependency-free; vendor it as-is. Resolve lazily (TestBench may load after you):
//
//   auto get = reinterpret_cast<void* (*)(unsigned)>(GetProcAddress(GetModuleHandleW(L"TestBench.dll"), "TestBench_GetInterface"));
//   auto* tb = get ? static_cast<TestBenchAPI::ITestBenchInterface001*>(get(1)) : nullptr;
#pragma once

namespace TestBenchAPI
{
	using WriteFn = void (*)(void* a_sink, const char* a_resultJson);
	// Runs on TestBench's listener thread; marshal to the game thread yourself when touching game state.
	using ToolFn = void (*)(void* a_ctx, const char* a_argsJson, void* a_sink, WriteFn a_write);

	struct ITestBenchInterface001
	{
		virtual unsigned int GetBuildNumber() = 0;   // MAJOR*10000 + MINOR*100 + PATCH
		// a_descriptorJson: { "description": str, "inputSchema": obj, "readOnly": bool }
		virtual bool RegisterTool(const char* a_name, const char* a_descriptorJson, ToolFn a_handler, void* a_ctx) = 0;
		virtual void EmitEvent(const char* a_topic, const char* a_payloadJson) = 0;
	};
}
