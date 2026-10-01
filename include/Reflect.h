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

	// true while the object array's slot for a_obj still holds a_obj. a_obj must be live NOW (just found or just handed
	// in by the engine): the slot index is read from the object itself, under a fault guard, so a freed pointer returns
	// false instead of crashing - but a freed object's slot and address can both be reused (logic library 8032). An
	// object kept across frames is kept in a Handle, never as a raw pointer checked with this.
	bool IsLive(UE::UObject* a_obj);

	// an object kept across frames (HUD Position Manager's ue::Handle, 2026-09-29): its slot, class and FName recorded
	// while it is live; Get() asks the object array's slot first and reads the object only once the slot still holds
	// it, so a garbage-collected object is never touched (AMF crashed in IsLive on a freed System page, 2026-09-30)
	struct Handle
	{
		UE::UObject*  ptr = nullptr;
		std::int32_t  index = -1;
		UE::UClass*   cls = nullptr;
		std::uint64_t name = 0;   // the FName's 8 bytes

		Handle() = default;
		explicit Handle(UE::UObject* a_live) { Set(a_live); }
		void         Set(UE::UObject* a_live);   // a_live must be live now
		UE::UObject* Get() const;                // nullptr once the slot holds anything else
		bool         Is(const UE::UObject* a_o) const { return a_o && a_o == ptr && Get() == a_o; }
	};

	// calls a UFunction by name through ProcessEvent (params laid out by the caller); false when it has none
	bool Call(UE::UObject* a_obj, const wchar_t* a_function, void* a_params);
}
