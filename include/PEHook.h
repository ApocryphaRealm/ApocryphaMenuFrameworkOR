#pragma once

// ============================================================================================================
// ProcessEvent observers by class, through a vtable ENTRY swap (slot 0x4D) - no code is patched, so this coexists
// with UE4SS's ProcessEvent hook on the shared body in either load order (logic library 7555, 2026-09-29).
//
// Several classes may share one native vtable; each vtable is swapped once, the original pointer is remembered per
// vtable, and the one detour calls every handler registered for the object's class before the original. Handlers
// run on the game thread, before the function body.
// ============================================================================================================

namespace pe
{
	using Handler = void (*)(UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params);

	// Watches calls on objects whose class is exactly a_class. False (logged) when its vtable cannot be swapped.
	bool Watch(UE::UClass* a_class, Handler a_handler);

	// The UFunction's name, UTF-8 (cheap enough for a learn-once compare per UFunction pointer).
	std::string FunctionName(UE::UFunction* a_fn);

	std::string Utf8(const UE::FString& a_s);
}
