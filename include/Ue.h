#pragma once

#include "PEHook.h"
#include "Reflect.h"

// Small helpers over CommonLibOB64 for calling reflected functions by name (game thread only). From Tween Menu for
// Oblivion Remastered's Ue.h, without the input helpers.
namespace ue
{
	inline std::string NameOf(UE::UObject* a_o)
	{
		return a_o ? pe::Utf8(a_o->GetFName().ToString()) : std::string("null");
	}

	inline std::string PathOf(UE::UObject* a_o)
	{
		return a_o ? pe::Utf8(a_o->GetFullName()) : std::string("null");
	}

	inline UE::UClass* Class(const wchar_t* a_path)
	{
		return UE::StaticFindObject<UE::UClass>(nullptr, nullptr, a_path);
	}

	// an object property's value, by name (null when the object has no such property or it is unset)
	inline UE::UObject* ObjProp(UE::UObject* a_o, std::string_view a_name)
	{
		if (!a_o) return nullptr;
		const auto off = reflect::Offset(a_o->GetClass(), a_name);
		return off >= 0 ? *reflect::At<UE::UObject*>(a_o, off) : nullptr;
	}

	// A reflected call: parameters by name, laid out from the UFunction's own properties.
	class Call
	{
	public:
		Call(UE::UObject* a_obj, const wchar_t* a_fn) :
			m_obj(a_obj),
			m_fn(a_obj ? a_obj->FindFunction(UE::FName(a_fn, UE::EFindName::Find)) : nullptr)
		{
			if (m_fn) {
				m_params.assign(static_cast<std::size_t>(reinterpret_cast<UE::UStruct*>(m_fn)->propertiesSize), 0);
			}
		}
		explicit operator bool() const { return m_fn != nullptr; }
		UE::UFunction* Function() const { return m_fn; }
		void* At(std::string_view a_name)
		{
			if (!m_fn) {
				return nullptr;
			}
			const auto off = reflect::Offset(reinterpret_cast<UE::UStruct*>(m_fn), a_name);
			return off >= 0 ? m_params.data() + off : nullptr;
		}
		// the first parameter, whatever its name (a setter's single argument)
		void* First()
		{
			if (!m_fn) return nullptr;
			const auto fields = reflect::Fields(reinterpret_cast<UE::UStruct*>(m_fn));
			return fields.empty() ? nullptr : m_params.data() + fields.front().second;
		}
		template <class T>
		bool Set(std::string_view a_name, const T& a_value)
		{
			if (void* p = At(a_name)) {
				std::memcpy(p, &a_value, sizeof(T));
				return true;
			}
			return false;
		}
		template <class T>
		T Get(std::string_view a_name)
		{
			T v{};
			if (void* p = At(a_name)) {
				std::memcpy(&v, p, sizeof(T));
			}
			return v;
		}
		bool Run()
		{
			if (!m_fn || !m_obj) {
				return false;
			}
			m_obj->ProcessEvent(m_fn, m_params.data());
			return true;
		}

	private:
		UE::UObject*              m_obj;
		UE::UFunction*            m_fn;
		std::vector<std::uint8_t> m_params;
	};
}
