#pragma once

// ============================================================================================================
// Just enough Unreal reflection to read a property by NAME (CommonLibOB64 has no FProperty): walk a struct's
// FField chain (next +0x18, name +0x20, as CommonLibOB64's FField declares) and read FProperty::Offset_Internal
// at +0x44 (UE5's layout). reflect::SelfCheck() proves the offset on a property whose place is already known
// (VQuickKeysMenuViewModel::KeyIndex at 0xD0, read in game 2026-09-26) before anything else trusts it.
// Game thread only.
// ============================================================================================================

namespace reflect
{
	// -1 when the struct (or its supers) has no property of that name
	std::int32_t Offset(UE::UStruct* a_struct, std::string_view a_name);

	// a property's size in bytes (FProperty::ElementSize at +0x34); -1 when there is no such property
	std::int32_t Size(UE::UStruct* a_struct, std::string_view a_name);

	// a struct's own properties (a UFunction's parameters), in declaration order: name and offset
	std::vector<std::pair<std::string, std::int32_t>> Fields(UE::UStruct* a_struct);

	bool SelfCheck();   // true once the Offset_Internal layout is proven; everything reflected refuses to run until then
	bool Ok();

	bool        TextSet(const UE::FText& a_text);   // false for a zeroed FText (a list row never given an item)
	std::string Text(const UE::FText& a_text);   // an FText's display string (UTF-8); empty for a zeroed one

	// the string-table key an FText was made from ("LOC_FN_..." for a form's name), through the engine's own
	// KismetTextLibrary::StringTableIdAndKeyFromText; empty when the text is not from a table
	std::string TextKey(const UE::FText& a_text);

	template <class T>
	T* At(void* a_base, std::int32_t a_offset)
	{
		return a_base && a_offset >= 0 ? reinterpret_cast<T*>(static_cast<std::uint8_t*>(a_base) + a_offset) : nullptr;
	}

	// every live instance of a class (never the class default object)
	std::vector<UE::UObject*> Instances(UE::UClass* a_class);
	bool IsLive(UE::UObject* a_obj);

	// calls a UFunction by name through ProcessEvent (params laid out by the caller); false when it has none
	bool Call(UE::UObject* a_obj, const wchar_t* a_function, void* a_params);
}
