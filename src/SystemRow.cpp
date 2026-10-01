// ============================================================================================================
// THE SYSTEM-MENU ROW, Oblivion Remastered (1.0.4, the owner 2026-09-29: "add to the system row in Oblivion an
// Apocrypha Menu Framework row to call AMF on controller").
//
// The game's System page (WBP_Modern_Settings_SaveLoadPage_C, the first page of WBP_Modern_SettingsMenu) is a column
// of rows of one class, WBP_Modern_Settings_SystemButtonWrapper_C: Save, Quick save, Load, Main menu, Quit. Each row
// is a VNavigableActivatableWidgetBase whose designer-set WidgetForNavigation map (direction -> widget) is what the
// controller's up / down walks; its inner CommonUI button fires the row's bound event
// "...CommonButtonBaseClicked__DelegateSignature" through ProcessEvent, and the page binds the row's OnButtonClicked
// delegate to what it does. Learned in game with TestBench (ue.find / ue.props / ue.hex / ue.call dry), 2026-09-29.
//
// So, once per live page: a sixth row is CREATED from the same class (WidgetBlueprintLibrary::Create), added to the
// panel that holds the others with the last row's slot layout copied, given its text (SetButtonText with an FText the
// engine's own Conv_StringToText makes), and spliced into the navigation maps - the last row's Down now leads to it,
// its Up leads back, and if the game wrapped the last row round to the first, the new row wraps instead. Pressing it
// is seen by a ProcessEvent watch on the row class (pe::Watch, a vtable-slot swap that coexists with UE4SS) and opens
// the framework's window; nothing is bound on the page, so the game's own rows are never touched.
//
// Nothing here is shipped as an asset: the row is added to the live menu, so it works with any menu artwork and with
// any other plugin that changes the page. Every step is checked and logged, and a page whose shape is not the one
// above (a game patch, another mod's rebuild) gets no row and a warning - never a half-wired one.
//
// PROVEN 2026-09-29 (the owner, 12:5x: "the system row for AMF works properly now"): what made the D-pad reach the row
// was ORDER. The navigation subsystem reads a row's parent, handle and map when the row is CONSTRUCTED (added to a
// panel); edits afterwards are invisible to it, though every reflected property looks right. So the new row is given
// its parent, handle and Up entry before it is added, and the last game row is taken out of the panel and put back
// (constructed again, its Down entry now leading to ours) just before ours goes in, which also keeps the order.
//
// The map surgery: TMap<enum, TObjectPtr<UWidget>> is a TSet<TPair<uint8, UObject*>> - a sparse array of 24-byte
// elements (key, value, hash-next, hash-bucket), an inline bit array of allocation flags, a free list, and a hash
// whose bucket count is 1 until the map has four elements (UE's TSetAllocator). The load row's map, read raw in
// game, showed exactly that: two elements, both in bucket 0, chained. Replacing a value touches one pointer; adding
// an element appends to the sparse array (grown through the engine's FMemory so the engine can free it), sets its
// flag bit, and pushes it onto the bucket chain. A map with a free list, or a hash that is not a power of two, is
// left alone (logged) rather than guessed at.
// ============================================================================================================
#include "SystemRow.h"

#include "PEHook.h"
#include "Reflect.h"
#include "Renderer.h"
#include "Settings.h"
#include "Ue.h"

namespace systemrow
{
	namespace
	{
		constexpr const wchar_t* kPageClass = L"/Game/UI/Modern/MenuLayer/Settings/Prefabs/WBP_Modern_Settings_SaveLoadPage.WBP_Modern_Settings_SaveLoadPage_C";
		constexpr const wchar_t* kRowClass = L"/Game/UI/Modern/MenuLayer/Settings/Prefabs/WBP_Modern_Settings_SystemButtonWrapper.WBP_Modern_Settings_SystemButtonWrapper_C";
		constexpr const wchar_t* kRowText = L"Apocrypha Menu Framework";

		// ---- the raw TMap<uint8 enum, UObject*> (80 bytes, UE 5.3 layout; see the file comment) ----------------
		struct RawMapEntry
		{
			std::uint64_t key;   // the enum, padded to the pair's pointer alignment
			UE::UObject*  value;
			std::int32_t  hashNext;   // the next element in the bucket's chain, -1 at the end
			std::int32_t  hashIndex;  // the bucket
		};
		static_assert(sizeof(RawMapEntry) == 24);

		struct RawMap
		{
			RawMapEntry*  data;
			std::int32_t  num, max;            // TSparseArray::Data (TArray)
			std::uint32_t inlineBits[4];       // TBitArray<TInlineAllocator<4>>: the allocation flags
			std::uint32_t* bitsData;
			std::int32_t  numBits, maxBits;
			std::int32_t  firstFree, numFree;  // the free list (holes left by removals)
			std::int32_t  inlineHash, pad0;    // THashAllocator<TInlineAllocator<1>>: the one inline bucket
			std::int32_t* hashData;
			std::int32_t  hashSize, pad1;
		};
		static_assert(sizeof(RawMap) == 80);

		bool MapSane(const RawMap& m)
		{
			if (m.num < 0 || m.max < m.num || m.numBits != m.num || m.numFree != 0) return false;
			if (m.num > 0 && !m.data) return false;
			if (m.hashSize < 0 || (m.hashSize & (m.hashSize - 1)) != 0) return false;   // 0 or a power of two
			if (m.num > 0 && m.hashSize == 0) return false;
			return true;
		}

		std::int32_t* Buckets(RawMap& m) { return m.hashData ? m.hashData : &m.inlineHash; }

		RawMap* NavMap(UE::UObject* a_row)
		{
			if (!a_row) return nullptr;
			const auto off = reflect::Offset(a_row->GetClass(), "WidgetForNavigation");
			if (off < 0 || reflect::Size(a_row->GetClass(), "WidgetForNavigation") != sizeof(RawMap)) return nullptr;
			return reflect::At<RawMap>(a_row, off);
		}

		UE::UObject* NavGet(UE::UObject* a_row, std::uint64_t a_key)
		{
			auto* m = NavMap(a_row);
			if (!m || !MapSane(*m)) return nullptr;
			for (std::int32_t i = 0; i < m->num; ++i) {
				if (m->data[i].key == a_key) return m->data[i].value;
			}
			return nullptr;
		}

		// every (key -> value) of a row's map, for the log and for learning the direction keys
		std::vector<std::pair<std::uint64_t, UE::UObject*>> NavEntries(UE::UObject* a_row)
		{
			std::vector<std::pair<std::uint64_t, UE::UObject*>> out;
			auto* m = NavMap(a_row);
			if (!m || !MapSane(*m)) return out;
			for (std::int32_t i = 0; i < m->num; ++i) out.emplace_back(m->data[i].key, m->data[i].value);
			return out;
		}

		bool NavSet(UE::UObject* a_row, std::uint64_t a_key, UE::UObject* a_value)
		{
			auto* m = NavMap(a_row);
			if (!m) {
				logger::warn("system row: {} has no WidgetForNavigation map of the expected size", ue::NameOf(a_row));
				return false;
			}
			if (!MapSane(*m)) {
				logger::warn("system row: {}'s navigation map is not in the shape this code knows (num {}, max {}, bits {}, free {}, hash {}) - left alone",
					ue::NameOf(a_row), m->num, m->max, m->numBits, m->numFree, m->hashSize);
				return false;
			}
			for (std::int32_t i = 0; i < m->num; ++i) {
				if (m->data[i].key == a_key) {
					m->data[i].value = a_value;   // a value swap: the hash is keyed on the direction, unchanged
					return true;
				}
			}
			// append
			if (m->num == m->max) {
				const std::int32_t newMax = m->max + 4;
				auto* grown = static_cast<RawMapEntry*>(UE::FMemory::Realloc(m->data, static_cast<std::size_t>(newMax) * sizeof(RawMapEntry)));
				if (!grown) {
					logger::error("system row: the engine's allocator refused {} bytes for {}'s navigation map", newMax * sizeof(RawMapEntry), ue::NameOf(a_row));
					return false;
				}
				m->data = grown;
				m->max = newMax;
			}
			const std::int32_t idx = m->num;
			if (m->bitsData) {
				if (idx >= m->maxBits) {
					logger::warn("system row: {}'s allocation-flag bits are heap-allocated and full - not appended", ue::NameOf(a_row));
					return false;
				}
				m->bitsData[idx / 32] |= 1u << (idx % 32);
			} else {
				if (idx >= 128) {
					logger::warn("system row: {}'s navigation map is unexpectedly large - not appended", ue::NameOf(a_row));
					return false;
				}
				m->inlineBits[idx / 32] |= 1u << (idx % 32);
				if (m->maxBits < 128) m->maxBits = 128;
			}
			if (m->hashSize == 0) {   // an empty map: one bucket, empty chain, as the engine makes it for its first element
				m->hashSize = 1;
				m->inlineHash = -1;
			}
			std::int32_t* buckets = Buckets(*m);
			const std::int32_t bucket = static_cast<std::int32_t>(a_key & static_cast<std::uint64_t>(m->hashSize - 1));   // GetTypeHash(uint8) is the value
			m->data[idx] = { a_key, a_value, buckets[bucket], bucket };
			buckets[bucket] = idx;
			m->num = idx + 1;
			m->numBits = m->num;
			return true;
		}

		// ---- the page's own Buttons array and FocusIndex -----------------------------------------------------------
		// The owner, 2026-09-29 12:3x: "the system row did appear, but I can't select it with the sticks or the D-pad,
		// only with the mouse". The page walks ITS OWN TArray Buttons with FocusIndex (CreateButtonArray, DoesAllow-
		// Navigation, BP_GetDesiredFocusTarget), not the rows' maps - so the row goes into that array too, and the
		// focus index follows it when the row takes focus (the page binds OnButtonFocussed only for its own rows).
		struct RawArray
		{
			UE::UObject** data;
			std::int32_t  num, max;
		};

		RawArray* Buttons(UE::UObject* a_page)
		{
			const auto off = a_page ? reflect::Offset(a_page->GetClass(), "Buttons") : -1;
			return off >= 0 ? reflect::At<RawArray>(a_page, off) : nullptr;
		}

		std::int32_t IndexIn(const RawArray* a_arr, UE::UObject* a_o)
		{
			for (std::int32_t i = 0; a_arr && a_arr->data && i < a_arr->num; ++i) {
				if (a_arr->data[i] == a_o) return i;
			}
			return -1;
		}

		bool AppendButton(UE::UObject* a_page, UE::UObject* a_row)
		{
			auto* arr = Buttons(a_page);
			if (!arr) {
				logger::warn("system row: the page has no Buttons array - the D-pad will not reach the row");
				return false;
			}
			if (IndexIn(arr, a_row) >= 0) return true;
			if (arr->num < 0 || arr->max < arr->num) return false;
			if (arr->num == arr->max) {
				const std::int32_t newMax = arr->max + 4;
				auto* grown = static_cast<UE::UObject**>(UE::FMemory::Realloc(arr->data, static_cast<std::size_t>(newMax) * sizeof(UE::UObject*)));
				if (!grown) return false;
				arr->data = grown;
				arr->max = newMax;
			}
			arr->data[arr->num++] = a_row;
			return true;
		}

		// Slate's own focus navigation: the explicit rules the page's CreateButtonArray gives its rows (UWidget::
		// SetNavigationRuleExplicit, Up = 2, Down = 3 in EUINavigation), set on the wrapper AND its inner button -
		// whichever of the two holds keyboard focus, pressing down on the last row now reaches ours and up comes back.
		// (The owner, 2026-09-29 12:5x: the maps and the Buttons array alone did not give the D-pad the row.)
		void Rule(UE::UObject* a_from, std::uint8_t a_direction, UE::UObject* a_to)
		{
			ue::Call c(a_from, L"SetNavigationRuleExplicit");
			if (!c || !a_from || !a_to) return;
			c.Set<std::uint8_t>("Direction", a_direction);
			c.Set("InWidget", a_to);
			c.Run();
		}

		void WireSlateNav(UE::UObject* a_last, UE::UObject* a_row)
		{
			constexpr std::uint8_t kUp = 2, kDown = 3;
			Rule(a_last, kDown, a_row);
			Rule(a_row, kUp, a_last);
			auto* lastButton = ue::ObjProp(a_last, "SaveLoadButton");
			auto* rowButton = ue::ObjProp(a_row, "SaveLoadButton");
			if (lastButton && rowButton) {
				Rule(lastButton, kDown, rowButton);
				Rule(rowButton, kUp, lastButton);
				Rule(lastButton, kDown, a_row);   // and across: a button's rule may name the wrapper
			}
		}

		void SetFocusIndex(UE::UObject* a_page, std::int32_t a_index)
		{
			const auto off = a_page ? reflect::Offset(a_page->GetClass(), "FocusIndex") : -1;
			if (off >= 0) *reflect::At<std::int32_t>(a_page, off) = a_index;
		}

		// ---- state ---------------------------------------------------------------------------------------------
		struct Injected
		{
			// kept as handles, never raw pointers: the game garbage-collects the System page and its rows, and a freed
			// pointer read to ask "is it alive" crashed the game (2026-09-30, equipping a loadout)
			reflect::Handle page;
			reflect::Handle row;
			reflect::Handle last;   // the game's last row, the one above ours
		};
		std::vector<Injected>        g_injected;   // game thread
		std::vector<reflect::Handle> g_refused;    // pages this code gave up on (no second try, no log spam)
		UE::UClass*              g_rowClass = nullptr;
		bool                     g_watching = false;
		std::atomic<bool>        g_everInjected{ false };
		std::mutex               g_reportLock;
		std::string              g_report = "[]";   // ListJson's answer, any thread
		std::string              g_foundPath;
		std::uint64_t            g_frame = 0;

		bool IsOurRow(UE::UObject* a_o)
		{
			for (const auto& i : g_injected) {
				if (i.row.Is(a_o)) return true;
			}
			return false;
		}

		// ProcessEvent on every row of the class: our row's click opens the framework
		bool Interesting(const std::string& a_name)
		{
			return a_name.find("Navigate") != std::string::npos || a_name.find("Focus") != std::string::npos ||
			       a_name.find("Activat") != std::string::npos || a_name.find("Hover") != std::string::npos ||
			       a_name.find("Button") != std::string::npos || a_name.find("Allow") != std::string::npos ||
			       a_name.find("Input") != std::string::npos || a_name.find("Commited") != std::string::npos;
		}

		// the page's events (diagnosis, 2026-09-29: which widget the D-pad drives - the page, a row, or its button)
		void OnPageEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void*)
		{
			const std::string name = pe::FunctionName(a_fn);
			if (!Interesting(name) || name == "Tick") return;
			const auto off = reflect::Offset(a_obj->GetClass(), "FocusIndex");
			logger::debug("system row: PAGE event {} (FocusIndex {})", name, off >= 0 ? *reflect::At<std::int32_t>(a_obj, off) : -1);
		}

		void OnRowEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void*)
		{
			const std::string name = pe::FunctionName(a_fn);
			if (Interesting(name) && name.find("Clicked") == std::string::npos) {
				logger::debug("system row: ROW event {} on {}{}", name, ue::NameOf(a_obj), IsOurRow(a_obj) ? " (ours)" : "");
			}
			if (!IsOurRow(a_obj)) return;
			if (name.find("CommonButtonBaseClicked") != std::string::npos || name == "OnButtonClicked__DelegateSignature") {
				logger::info("system row: pressed ({}) - opening the framework", name);
				renderer::SetMenuVisible(true, true);   // nested: placed to the right of the page's rows (Renderer.cpp, the nested default)
			} else if (name.find("OnButtonFocussed") != std::string::npos || name == "OnFocus") {
				for (const auto& i : g_injected) {
					auto* page = i.row.Is(a_obj) ? i.page.Get() : nullptr;
					if (page) {
						if (const auto idx = IndexIn(Buttons(page), a_obj); idx >= 0) {
							SetFocusIndex(page, idx);   // as the page does for its own rows in their focus handlers
						}
					}
				}
			} else {
				static std::vector<std::string> seen;
				if (seen.size() < 24 && std::find(seen.begin(), seen.end(), name) == seen.end()) {
					seen.push_back(name);
					logger::debug("system row: event on our row: {}", name);
				}
			}
		}

		// the rows of the page's class in the panel's order: the last row's parent panel and its slots' contents
		std::vector<UE::UObject*> RowsInOrder(UE::UObject* a_panel)
		{
			std::vector<UE::UObject*> out;
			const auto off = a_panel ? reflect::Offset(a_panel->GetClass(), "Slots") : -1;
			if (off < 0) return out;
			struct RawArray { UE::UObject** data; std::int32_t num, max; };
			const auto* slots = reflect::At<RawArray>(a_panel, off);
			for (std::int32_t i = 0; slots->data && i < slots->num; ++i) {
				auto* content = ue::ObjProp(slots->data[i], "Content");
				if (content && content->GetClass() == g_rowClass) out.push_back(content);
			}
			return out;
		}

		// a slot's layout (padding, size, alignments) read off one slot and applied to another through its setters
		struct SlotLayout
		{
			std::vector<std::pair<std::string, std::vector<std::uint8_t>>> values;
			void Read(UE::UObject* a_slot)
			{
				values.clear();
				for (const char* prop : { "Padding", "Size", "HorizontalAlignment", "VerticalAlignment" }) {
					const auto off = reflect::Offset(a_slot->GetClass(), prop);
					const auto size = reflect::Size(a_slot->GetClass(), prop);
					if (off >= 0 && size > 0) {
						auto* src = reflect::At<std::uint8_t>(a_slot, off);
						values.emplace_back(prop, std::vector<std::uint8_t>(src, src + size));
					}
				}
			}
			void Apply(UE::UObject* a_slot) const
			{
				for (const auto& [prop, bytes] : values) {
					const wchar_t* setter = prop == "Padding" ? L"SetPadding" : prop == "Size" ? L"SetSize" : prop == "HorizontalAlignment" ? L"SetHorizontalAlignment" : L"SetVerticalAlignment";
					ue::Call set(a_slot, setter);
					void* first = set ? set.First() : nullptr;
					if (first) {
						std::memcpy(first, bytes.data(), bytes.size());
						set.Run();
					}
				}
			}
		};

		UE::UObject* AddToPanel(UE::UObject* a_panel, UE::UObject* a_widget)
		{
			for (const wchar_t* fn : { L"AddChild", L"AddChildToVerticalBox", L"AddChildToHorizontalBox", L"AddChildToScrollBox", L"AddChildToOverlay", L"AddChildToWrapBox", L"AddChildToStackBox" }) {
				ue::Call add(a_panel, fn);
				if (!add) continue;
				add.Set("Content", a_widget);
				add.Run();
				return add.Get<UE::UObject*>("ReturnValue");
			}
			return nullptr;
		}

		bool Inject(UE::UObject* a_page)
		{
			auto* quit = ue::ObjProp(a_page, "settings_system_quit_button");
			auto* save = ue::ObjProp(a_page, "settings_system_save_button");
			if (!quit || !save) {
				logger::warn("system row: the System page has no settings_system_quit_button / settings_system_save_button - no row");
				return false;
			}
			auto* quitSlot = ue::ObjProp(quit, "Slot");
			auto* panel = ue::ObjProp(quitSlot, "Parent");
			if (!panel) {
				return false;   // not laid out yet: asked again next time round
			}
			const auto rows = RowsInOrder(panel);
			if (rows.size() < 3) {
				logger::warn("system row: the panel {} holds {} rows of the row class - not the shape this code knows; no row", ue::NameOf(panel), rows.size());
				return false;
			}
			// learn the direction keys from a middle row: the entry leading to the row above is Up, to the row below Down
			std::int64_t upKey = -1, downKey = -1;
			for (std::size_t i = 1; i + 1 < rows.size() && (upKey < 0 || downKey < 0); ++i) {
				for (const auto& [k, v] : NavEntries(rows[i])) {
					if (v == rows[i - 1]) upKey = static_cast<std::int64_t>(k);
					if (v == rows[i + 1]) downKey = static_cast<std::int64_t>(k);
				}
			}
			for (std::size_t i = 0; i < rows.size(); ++i) {
				std::string e;
				for (const auto& [k, v] : NavEntries(rows[i])) e += std::format("{}->{} ", k, ue::NameOf(v));
				logger::info("system row: row {} {} nav [{}]", i, ue::NameOf(rows[i]), e);
			}
			if (upKey < 0 || downKey < 0) {
				logger::warn("system row: the direction keys could not be learned from the rows' maps - no row");
				return false;
			}
			auto* last = rows.back();
			auto* first = rows.front();
			if (last != quit) {
				logger::info("system row: the last row is {} (not Quit) - the new row goes under it", ue::NameOf(last));
			}
			auto* lastSlot = ue::ObjProp(last, "Slot");
			if (!lastSlot) return false;
			SlotLayout layout;
			layout.Read(lastSlot);

			// ---- create, and set everything the subsystem reads at construction BEFORE the row is constructed ----
			auto* lib = ue::Class(L"/Script/UMG.WidgetBlueprintLibrary");
			auto* libCdo = lib ? lib->GetDefaultObject(false) : nullptr;
			ue::Call create(libCdo, L"Create");
			if (!create) {
				logger::warn("system row: WidgetBlueprintLibrary::Create is not reflected - no row");
				return false;
			}
			if (ue::Dying(a_page)) return false;
			create.Set("WorldContextObject", a_page);
			create.Set("WidgetType", g_rowClass);
			if (!create.RunGuarded()) {
				logger::warn("system row: WidgetBlueprintLibrary::Create faulted - no row");
				return false;
			}
			auto* row = create.Get<UE::UObject*>("ReturnValue");
			if (!row) {
				logger::warn("system row: Create returned no widget - no row");
				return false;
			}
			{
				auto* cls = row->GetClass();
				for (const char* prop : { "NavigableParent", "UINavigationSubsystem" }) {   // the page, and the subsystem's handle, as the game's rows carry them
					const auto off = reflect::Offset(cls, prop);
					const auto size = reflect::Size(cls, prop);
					if (off >= 0 && size > 0 && reflect::Offset(last->GetClass(), prop) == off) {
						std::memcpy(reflect::At<std::uint8_t>(row, off), reflect::At<std::uint8_t>(last, off), static_cast<std::size_t>(size));
					}
				}
			}
			auto* wrap = NavGet(last, static_cast<std::uint64_t>(downKey));   // did the last row lead round to the first?
			bool ok = NavSet(row, static_cast<std::uint64_t>(upKey), last);
			ok = NavSet(last, static_cast<std::uint64_t>(downKey), row) && ok;
			if (wrap) {
				ok = NavSet(row, static_cast<std::uint64_t>(downKey), wrap) && ok;
				if (NavGet(first, static_cast<std::uint64_t>(upKey)) == last) {
					ok = NavSet(first, static_cast<std::uint64_t>(upKey), row) && ok;
				}
			}
			logger::info("system row: navigation {} (up key {}, down key {}, {} wrapped round to {})", ok ? "wired" : "PARTLY wired", upKey, downKey,
				ue::NameOf(last), wrap ? ue::NameOf(wrap) : "nothing");

			// ---- the last row out of the panel and back (constructed again, with its map now leading to ours), then ours ----
			{
				ue::Call remove(last, L"RemoveFromParent");
				const bool removed = remove && remove.Run();
				auto* newLastSlot = removed ? AddToPanel(panel, last) : nullptr;
				if (newLastSlot) {
					layout.Apply(newLastSlot);
				}
				logger::info("system row: {} taken out of the panel and put back ({}) so it is constructed again", ue::NameOf(last),
					newLastSlot ? ue::NameOf(newLastSlot->GetClass()) : "FAILED - it may be gone from the page");
			}
			auto* slot = AddToPanel(panel, row);
			if (!slot) {
				logger::warn("system row: the panel {} ({}) takes no child through any reflected AddChild - no row", ue::NameOf(panel), ue::NameOf(panel->GetClass()));
				return false;
			}
			layout.Apply(slot);
			logger::info("system row: added to {} ({}) -> slot {}", ue::NameOf(panel), ue::NameOf(panel->GetClass()), ue::NameOf(slot->GetClass()));
			for (auto* w : { last, row }) {
				ue::Call sync(w, L"ForceSynchronizeProperties");
				if (sync) sync.Run();
			}
			// the text: an FText the engine makes from our string, handed to the row's own SetButtonText
			{
				auto* textLib = ue::Class(L"/Script/Engine.KismetTextLibrary");
				ue::Call conv(textLib ? textLib->GetDefaultObject(false) : nullptr, L"Conv_StringToText");
				ue::Call setText(row, L"SetButtonText");
				void* inString = conv ? conv.At("InString") : nullptr;
				void* ret = conv ? conv.At("ReturnValue") : nullptr;
				void* newText = setText ? setText.At("NewText") : nullptr;
				if (inString && ret && newText) {
					new (inString) UE::FString(kRowText);   // destroyed by ProcessEvent with the other parameters
					conv.Run();
					std::memcpy(newText, ret, 24);          // the FText the engine built, moved into the setter's parameter
					setText.Run();
				} else {
					logger::warn("system row: no Conv_StringToText / SetButtonText - the row has no text");
				}
			}
			{
				const bool inArray = AppendButton(a_page, row);
				const auto* arr = Buttons(a_page);
				logger::info("system row: the page's Buttons array {} the row ({} entries)", inArray ? "holds" : "does NOT hold", arr ? arr->num : -1);
			}
			WireSlateNav(last, row);
			g_injected.push_back({ reflect::Handle(a_page), reflect::Handle(row), reflect::Handle(last) });
			g_everInjected.store(true);
			g_foundPath = ue::PathOf(a_page);
			logger::info("system row: \"{}\" added to the System page {} as row {} of {}", pe::Utf8(UE::FString(kRowText)), ue::NameOf(a_page), rows.size() + 1, rows.size() + 1);
			return true;
		}

		void Report()
		{
			std::string s = "[";
			for (std::size_t i = 0; i < g_injected.size(); ++i) {
				auto* page = g_injected[i].page.Get();
				auto* row = g_injected[i].row.Get();
				s += std::format("{}{{\"page\":\"{}\",\"row\":\"{}\",\"live\":{}}}", i ? "," : "", page ? ue::NameOf(page) : std::string("gone"),
					row ? ue::NameOf(row) : std::string("gone"), page ? "true" : "false");
			}
			s += "]";
			std::scoped_lock l(g_reportLock);
			g_report = s;
		}
	}

	void Tick()
	{
		if (!settings::Get().systemMenuRow) return;
		if (++g_frame % 30 != 0) return;   // twice a second is plenty for a menu that opens by hand
		if (!reflect::SelfCheck()) return;
		if (!g_rowClass) {
			g_rowClass = ue::Class(kRowClass);
			if (!g_rowClass) return;   // not loaded yet
		}
		if (!g_watching) {
			g_watching = pe::Watch(g_rowClass, &OnRowEvent);
			if (!g_watching) {
				logger::warn("system row: the row class cannot be watched - a row would do nothing when pressed; none is added");
				return;
			}
		}
		auto* pageClass = ue::Class(kPageClass);
		if (!pageClass) return;
		static bool pageWatched = false;
		if (!pageWatched) pageWatched = pe::Watch(pageClass, &OnPageEvent);
		// forget pages that are gone
		const auto before = g_injected.size();
		std::erase_if(g_injected, [](const Injected& i) { return !i.page.Get(); });
		if (g_injected.size() != before) logger::debug("system row: {} System page(s) garbage-collected - forgotten", before - g_injected.size());
		std::erase_if(g_refused, [](const reflect::Handle& p) { return !p.Get(); });
		for (const auto& i : g_injected) {
			auto* page = i.page.Get();
			auto* row = i.row.Get();
			if (!page || !row) continue;
			if (IndexIn(Buttons(page), row) < 0) {
				if (AppendButton(page, row)) logger::info("system row: the page rebuilt its Buttons array - the row put back into it");
			}
			if (g_frame % 60 == 0) {
				if (auto* last = i.last.Get()) {
					WireSlateNav(last, row);   // the page may set its rows' rules again on each activation: ours are set again too
				}
			}
		}
		for (auto* page : reflect::Instances(pageClass)) {
			const bool done = std::ranges::any_of(g_injected, [&](const Injected& i) { return i.page.Is(page); });
			if (done || std::ranges::any_of(g_refused, [&](const reflect::Handle& r) { return r.Is(page); })) continue;
			if (!Inject(page)) {
				// a page not laid out yet is tried again; one this code warned about is not
				if (ue::ObjProp(ue::ObjProp(ue::ObjProp(page, "settings_system_quit_button"), "Slot"), "Parent")) {
					g_refused.emplace_back(page);
				}
			}
		}
		Report();
	}

	bool        Install() { return true; }   // the row is added from Tick(); nothing to register up front
	bool        WasInjected() { return g_everInjected.load(); }
	const char* FoundPath() { return g_foundPath.c_str(); }
	std::string ListJson()
	{
		std::scoped_lock l(g_reportLock);
		return g_report;
	}
	// The window is not sized to the game's page (M3 puts AMF over the pause menu; the journal-panel geometry is Skyrim's).
	bool        GetPanelRect(float&, float&, float&, float&) { return false; }
	float       PaneLeft() { return 0.0f; }
	const char* ArtKey() { return ""; }
	bool        MeasurePath(const std::string&, float&, float&, float&, float&) { return false; }
}
