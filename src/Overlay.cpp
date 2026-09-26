#include "Overlay.h"

#include "Menu.h"

#include <MinHook.h>

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace Overlay
{
	namespace
	{
		// ---- state ------------------------------------------------------------------------------------------
		std::atomic_bool         g_open{ false };
		ID3D12CommandQueue*      g_queue = nullptr;         // the presenting queue, captured at swap-chain creation
		IDXGISwapChain*          g_swapChain = nullptr;     // the swap chain we initialised for
		ID3D12Device*            g_device = nullptr;
		ID3D12DescriptorHeap*    g_rtvHeap = nullptr;
		ID3D12DescriptorHeap*    g_srvHeap = nullptr;
		ID3D12GraphicsCommandList* g_list = nullptr;
		ID3D12Fence*             g_fence = nullptr;
		HANDLE                   g_fenceEvent = nullptr;
		UINT64                   g_fenceValue = 0;
		HWND                     g_hwnd = nullptr;
		WNDPROC                  g_origWndProc = nullptr;
		DXGI_FORMAT              g_format = DXGI_FORMAT_R8G8B8A8_UNORM;
		bool                     g_ready = false;
		bool                     g_failed = false;
		thread_local bool        t_inPresent = false;

		struct Frame
		{
			ID3D12CommandAllocator*     allocator = nullptr;
			ID3D12Resource*             backBuffer = nullptr;
			D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
			UINT64                      fence = 0;
		};
		std::vector<Frame> g_frames;

		// ---- originals --------------------------------------------------------------------------------------
		using CreateFactory_t = HRESULT(WINAPI*)(REFIID, void**);
		using CreateFactory2_t = HRESULT(WINAPI*)(UINT, REFIID, void**);
		using CreateSwapChain_t = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
		using CreateSwapChainForHwnd_t = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
			const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
		using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
		using Present1_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
		using ResizeBuffers_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

		CreateFactory_t          o_CreateDXGIFactory = nullptr;
		CreateFactory_t          o_CreateDXGIFactory1 = nullptr;
		CreateFactory2_t         o_CreateDXGIFactory2 = nullptr;
		CreateSwapChain_t        o_CreateSwapChain = nullptr;
		CreateSwapChainForHwnd_t o_CreateSwapChainForHwnd = nullptr;
		Present_t                o_Present = nullptr;
		Present1_t               o_Present1 = nullptr;
		ResizeBuffers_t          o_ResizeBuffers = nullptr;

		template <class T>
		void Release(T*& a_p)
		{
			if (a_p) {
				a_p->Release();
				a_p = nullptr;
			}
		}

		// ---- vtable hooks (MinHook on the function the vtable slot points at; shared by every instance) ------
		template <class T>
		bool HookSlot(void* a_object, std::size_t a_index, void* a_detour, T& a_original, const char* a_what)
		{
			if (a_original) {
				return true;
			}
			if (!a_object) {
				return false;
			}
			auto** vtbl = *reinterpret_cast<void***>(a_object);
			void*  target = vtbl ? vtbl[a_index] : nullptr;
			if (!target) {
				REX::ERROR("hook {}: vtable slot {} is empty", a_what, a_index);
				return false;
			}
			if (MH_CreateHook(target, a_detour, reinterpret_cast<void**>(&a_original)) != MH_OK ||
				MH_EnableHook(target) != MH_OK) {
				REX::ERROR("hook {}: MinHook failed at {:p}", a_what, target);
				a_original = nullptr;
				return false;
			}
			REX::INFO("hooked {} at {:p}", a_what, target);
			return true;
		}

		// ---- render resources ---------------------------------------------------------------------------------
		void ReleaseBackBuffers()
		{
			for (auto& f : g_frames) {
				Release(f.backBuffer);
			}
		}

		void WaitIdle()
		{
			if (g_queue && g_fence && g_fenceEvent) {
				const UINT64 v = ++g_fenceValue;
				if (SUCCEEDED(g_queue->Signal(g_fence, v)) && g_fence->GetCompletedValue() < v) {
					g_fence->SetEventOnCompletion(v, g_fenceEvent);
					WaitForSingleObject(g_fenceEvent, 2000);
				}
			}
		}

		bool CreateBackBuffers(IDXGISwapChain* a_swapChain)
		{
			const UINT step = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			D3D12_CPU_DESCRIPTOR_HANDLE h = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
			for (UINT i = 0; i < g_frames.size(); ++i) {
				auto& f = g_frames[i];
				if (FAILED(a_swapChain->GetBuffer(i, IID_PPV_ARGS(&f.backBuffer)))) {
					REX::ERROR("GetBuffer({}) failed", i);
					return false;
				}
				f.rtv = h;
				g_device->CreateRenderTargetView(f.backBuffer, nullptr, h);
				h.ptr += step;
			}
			return true;
		}

		LRESULT CALLBACK WndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wp, LPARAM a_lp);

		bool Init(IDXGISwapChain* a_swapChain)
		{
			if (g_ready || g_failed) {
				return g_ready;
			}
			if (!g_queue) {
				REX::ERROR("no command queue was captured at swap-chain creation; the overlay stays off");
				g_failed = true;
				return false;
			}
			DXGI_SWAP_CHAIN_DESC desc{};
			if (FAILED(a_swapChain->GetDesc(&desc)) || FAILED(a_swapChain->GetDevice(IID_PPV_ARGS(&g_device)))) {
				REX::ERROR("swap chain: GetDesc/GetDevice failed");
				g_failed = true;
				return false;
			}
			g_hwnd = desc.OutputWindow;
			g_format = desc.BufferDesc.Format;
			g_frames.assign(desc.BufferCount, {});
			REX::INFO("swap chain {:p}: {}x{}, {} buffers, format {}, window {:p}", static_cast<void*>(a_swapChain),
				desc.BufferDesc.Width, desc.BufferDesc.Height, desc.BufferCount, static_cast<int>(g_format),
				static_cast<void*>(g_hwnd));

			D3D12_DESCRIPTOR_HEAP_DESC rtv{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV, desc.BufferCount, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
			D3D12_DESCRIPTOR_HEAP_DESC srv{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
			if (FAILED(g_device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&g_rtvHeap))) ||
				FAILED(g_device->CreateDescriptorHeap(&srv, IID_PPV_ARGS(&g_srvHeap)))) {
				REX::ERROR("descriptor heaps failed");
				g_failed = true;
				return false;
			}
			for (auto& f : g_frames) {
				if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.allocator)))) {
					REX::ERROR("command allocator failed");
					g_failed = true;
					return false;
				}
			}
			if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frames[0].allocator, nullptr, IID_PPV_ARGS(&g_list))) ||
				FAILED(g_list->Close()) ||
				FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)))) {
				REX::ERROR("command list / fence failed");
				g_failed = true;
				return false;
			}
			g_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			if (!CreateBackBuffers(a_swapChain)) {
				g_failed = true;
				return false;
			}

			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			ImGui_ImplWin32_Init(g_hwnd);
			ImGui_ImplDX12_Init(g_device, static_cast<int>(g_frames.size()), g_format, g_srvHeap,
				g_srvHeap->GetCPUDescriptorHandleForHeapStart(), g_srvHeap->GetGPUDescriptorHandleForHeapStart());
			Menu::ApplyStyle();

			g_origWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc)));
			g_swapChain = a_swapChain;
			g_ready = true;
			REX::INFO("overlay ready (Dear ImGui {}, {} frames in flight)", IMGUI_VERSION, g_frames.size());
			return true;
		}

		void Render(IDXGISwapChain* a_swapChain)
		{
			if (!Init(a_swapChain) || a_swapChain != g_swapChain) {
				return;
			}
			if (!g_open.load()) {
				return;   // nothing drawn, nothing recorded: a closed menu costs one flag read per frame
			}
			ImGuiIO& io = ImGui::GetIO();
			io.MouseDrawCursor = true;
			ClipCursor(nullptr);

			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			Menu::Draw();
			ImGui::Render();

			IDXGISwapChain3* sc3 = nullptr;
			if (FAILED(a_swapChain->QueryInterface(IID_PPV_ARGS(&sc3)))) {
				return;
			}
			const UINT idx = sc3->GetCurrentBackBufferIndex();
			sc3->Release();
			if (idx >= g_frames.size() || !g_frames[idx].backBuffer) {
				return;
			}
			Frame& f = g_frames[idx];
			if (f.fence && g_fence->GetCompletedValue() < f.fence) {
				g_fence->SetEventOnCompletion(f.fence, g_fenceEvent);
				WaitForSingleObject(g_fenceEvent, 1000);
			}
			f.allocator->Reset();
			g_list->Reset(f.allocator, nullptr);

			D3D12_RESOURCE_BARRIER b{};
			b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Transition.pResource = f.backBuffer;
			b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			g_list->ResourceBarrier(1, &b);
			g_list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
			g_list->SetDescriptorHeaps(1, &g_srvHeap);
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_list);
			std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
			g_list->ResourceBarrier(1, &b);
			g_list->Close();
			ID3D12CommandList* lists[] = { g_list };
			g_queue->ExecuteCommandLists(1, lists);
			f.fence = ++g_fenceValue;
			g_queue->Signal(g_fence, f.fence);
		}

		// ---- detours --------------------------------------------------------------------------------------------
		HRESULT STDMETHODCALLTYPE hk_Present(IDXGISwapChain* a_this, UINT a_sync, UINT a_flags)
		{
			if (!t_inPresent && !(a_flags & DXGI_PRESENT_TEST)) {
				t_inPresent = true;
				Render(a_this);
				const HRESULT hr = o_Present(a_this, a_sync, a_flags);
				t_inPresent = false;
				return hr;
			}
			return o_Present(a_this, a_sync, a_flags);
		}

		HRESULT STDMETHODCALLTYPE hk_Present1(IDXGISwapChain1* a_this, UINT a_sync, UINT a_flags, const DXGI_PRESENT_PARAMETERS* a_params)
		{
			if (!t_inPresent && !(a_flags & DXGI_PRESENT_TEST)) {
				t_inPresent = true;
				Render(a_this);
				const HRESULT hr = o_Present1(a_this, a_sync, a_flags, a_params);
				t_inPresent = false;
				return hr;
			}
			return o_Present1(a_this, a_sync, a_flags, a_params);
		}

		HRESULT STDMETHODCALLTYPE hk_ResizeBuffers(IDXGISwapChain* a_this, UINT a_count, UINT a_w, UINT a_h, DXGI_FORMAT a_fmt, UINT a_flags)
		{
			const bool ours = g_ready && a_this == g_swapChain;
			if (ours) {
				WaitIdle();
				ReleaseBackBuffers();
				ImGui_ImplDX12_InvalidateDeviceObjects();
			}
			const HRESULT hr = o_ResizeBuffers(a_this, a_count, a_w, a_h, a_fmt, a_flags);
			if (ours && SUCCEEDED(hr)) {
				DXGI_SWAP_CHAIN_DESC desc{};
				a_this->GetDesc(&desc);
				if (desc.BufferCount != g_frames.size()) {
					REX::WARN("buffer count changed {} -> {}; the overlay stays off until restart", g_frames.size(), desc.BufferCount);
					g_ready = false;
					g_failed = true;
					return hr;
				}
				CreateBackBuffers(a_this);
				ImGui_ImplDX12_CreateDeviceObjects();
				REX::INFO("resized to {}x{}", desc.BufferDesc.Width, desc.BufferDesc.Height);
			}
			return hr;
		}

		void CaptureQueue(IUnknown* a_device, const char* a_via)
		{
			ID3D12CommandQueue* q = nullptr;
			if (a_device && SUCCEEDED(a_device->QueryInterface(IID_PPV_ARGS(&q)))) {
				if (g_queue) {
					g_queue->Release();
				}
				g_queue = q;   // keep our reference
				REX::INFO("captured the presenting command queue {:p} via {}", static_cast<void*>(q), a_via);
			}
		}

		void HookSwapChain(IUnknown* a_swapChain)
		{
			IDXGISwapChain* sc = nullptr;
			if (!a_swapChain || FAILED(a_swapChain->QueryInterface(IID_PPV_ARGS(&sc)))) {
				return;
			}
			HookSlot(sc, 8, reinterpret_cast<void*>(&hk_Present), o_Present, "IDXGISwapChain::Present");
			HookSlot(sc, 13, reinterpret_cast<void*>(&hk_ResizeBuffers), o_ResizeBuffers, "IDXGISwapChain::ResizeBuffers");
			IDXGISwapChain1* sc1 = nullptr;
			if (SUCCEEDED(sc->QueryInterface(IID_PPV_ARGS(&sc1)))) {
				HookSlot(sc1, 22, reinterpret_cast<void*>(&hk_Present1), o_Present1, "IDXGISwapChain1::Present1");
				sc1->Release();
			}
			sc->Release();
		}

		HRESULT STDMETHODCALLTYPE hk_CreateSwapChain(IDXGIFactory* a_this, IUnknown* a_device, DXGI_SWAP_CHAIN_DESC* a_desc, IDXGISwapChain** a_out)
		{
			const HRESULT hr = o_CreateSwapChain(a_this, a_device, a_desc, a_out);
			if (SUCCEEDED(hr) && a_out && *a_out) {
				CaptureQueue(a_device, "CreateSwapChain");
				HookSwapChain(*a_out);
			}
			return hr;
		}

		HRESULT STDMETHODCALLTYPE hk_CreateSwapChainForHwnd(IDXGIFactory2* a_this, IUnknown* a_device, HWND a_hwnd, const DXGI_SWAP_CHAIN_DESC1* a_desc,
			const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_fs, IDXGIOutput* a_output, IDXGISwapChain1** a_out)
		{
			const HRESULT hr = o_CreateSwapChainForHwnd(a_this, a_device, a_hwnd, a_desc, a_fs, a_output, a_out);
			if (SUCCEEDED(hr) && a_out && *a_out) {
				CaptureQueue(a_device, "CreateSwapChainForHwnd");
				HookSwapChain(*a_out);
			}
			return hr;
		}

		void HookFactory(void* a_factory)
		{
			if (!a_factory) {
				return;
			}
			auto* unk = static_cast<IUnknown*>(a_factory);
			IDXGIFactory* f = nullptr;
			if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&f)))) {
				HookSlot(f, 10, reinterpret_cast<void*>(&hk_CreateSwapChain), o_CreateSwapChain, "IDXGIFactory::CreateSwapChain");
				f->Release();
			}
			IDXGIFactory2* f2 = nullptr;
			if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&f2)))) {
				HookSlot(f2, 15, reinterpret_cast<void*>(&hk_CreateSwapChainForHwnd), o_CreateSwapChainForHwnd, "IDXGIFactory2::CreateSwapChainForHwnd");
				f2->Release();
			}
		}

		HRESULT WINAPI hk_CreateDXGIFactory(REFIID a_riid, void** a_out)
		{
			const HRESULT hr = o_CreateDXGIFactory(a_riid, a_out);
			if (SUCCEEDED(hr) && a_out) {
				HookFactory(*a_out);
			}
			return hr;
		}

		HRESULT WINAPI hk_CreateDXGIFactory1(REFIID a_riid, void** a_out)
		{
			const HRESULT hr = o_CreateDXGIFactory1(a_riid, a_out);
			if (SUCCEEDED(hr) && a_out) {
				HookFactory(*a_out);
			}
			return hr;
		}

		HRESULT WINAPI hk_CreateDXGIFactory2(UINT a_flags, REFIID a_riid, void** a_out)
		{
			const HRESULT hr = o_CreateDXGIFactory2(a_flags, a_riid, a_out);
			if (SUCCEEDED(hr) && a_out) {
				HookFactory(*a_out);
			}
			return hr;
		}

		// ---- input ------------------------------------------------------------------------------------------------
		bool IsInputMessage(UINT a_msg)
		{
			return (a_msg >= WM_KEYFIRST && a_msg <= WM_KEYLAST) || (a_msg >= WM_MOUSEFIRST && a_msg <= WM_MOUSELAST) ||
			       a_msg == WM_INPUT;
		}

		LRESULT CALLBACK WndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wp, LPARAM a_lp)
		{
			if (a_msg == WM_KEYDOWN && a_wp == Menu::ToggleKey() && !(a_lp & (1 << 30))) {
				Toggle();
				return 0;
			}
			if (g_open.load() && g_ready) {
				ImGui_ImplWin32_WndProcHandler(a_hwnd, a_msg, a_wp, a_lp);
				if (a_msg == WM_INPUT) {
					return DefWindowProcW(a_hwnd, a_msg, a_wp, a_lp);   // consumed: the game's raw mouse/keys do not see it
				}
				if (IsInputMessage(a_msg)) {
					return 0;
				}
			}
			return CallWindowProcW(g_origWndProc, a_hwnd, a_msg, a_wp, a_lp);
		}
	}

	bool Install()
	{
		if (MH_Initialize() != MH_OK) {
			REX::ERROR("MinHook failed to initialise");
			return false;
		}
		HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
		if (!dxgi) {
			REX::ERROR("dxgi.dll did not load");
			return false;
		}
		struct Export
		{
			const char* name;
			void*       detour;
			void**      original;
		};
		const Export exports[] = {
			{ "CreateDXGIFactory", reinterpret_cast<void*>(&hk_CreateDXGIFactory), reinterpret_cast<void**>(&o_CreateDXGIFactory) },
			{ "CreateDXGIFactory1", reinterpret_cast<void*>(&hk_CreateDXGIFactory1), reinterpret_cast<void**>(&o_CreateDXGIFactory1) },
			{ "CreateDXGIFactory2", reinterpret_cast<void*>(&hk_CreateDXGIFactory2), reinterpret_cast<void**>(&o_CreateDXGIFactory2) },
		};
		int ok = 0;
		for (const auto& e : exports) {
			void* target = reinterpret_cast<void*>(GetProcAddress(dxgi, e.name));
			if (target && MH_CreateHook(target, e.detour, e.original) == MH_OK && MH_EnableHook(target) == MH_OK) {
				REX::INFO("hooked dxgi!{}", e.name);
				++ok;
			} else {
				REX::WARN("could not hook dxgi!{}", e.name);
			}
		}
		return ok > 0;
	}

	bool IsOpen() { return g_open.load(); }

	void SetOpen(bool a_open)
	{
		if (g_open.exchange(a_open) != a_open) {
			REX::INFO("menu {}", a_open ? "opened" : "closed");
		}
	}

	void Toggle() { SetOpen(!g_open.load()); }
}
