#include "Overlay.h"

#include "DevBenchTool.h"
#include "Gfx.h"
#include "Input.h"
#include "Renderer.h"

#include <MinHook.h>

#include <imgui.h>
#include <imgui_impl_dx12.h>

#include <wincodec.h>

namespace
{
	// ---- state ----------------------------------------------------------------------------------------------
	ID3D12CommandQueue*        g_queue = nullptr;         // the presenting queue, captured at swap-chain creation
	IDXGISwapChain*            g_swapChain = nullptr;     // the swap chain we initialised for
	ID3D12Device*              g_device = nullptr;
	ID3D12DescriptorHeap*      g_rtvHeap = nullptr;
	ID3D12DescriptorHeap*      g_srvHeap = nullptr;
	ID3D12GraphicsCommandList* g_list = nullptr;
	ID3D12Fence*               g_fence = nullptr;
	HANDLE                     g_fenceEvent = nullptr;
	std::atomic<UINT64>        g_fenceValue{ 0 };
	HWND                       g_hwnd = nullptr;
	WNDPROC                    g_origWndProc = nullptr;
	DXGI_FORMAT                g_format = DXGI_FORMAT_R8G8B8A8_UNORM;
	UINT                       g_width = 0, g_height = 0;
	std::atomic_bool           g_ready{ false };
	bool                       g_failed = false;
	bool                       g_backendUp = false;
	thread_local bool          t_inPresent = false;
	std::recursive_mutex       g_gpuLock;                 // one submitter at a time on our fence and the upload list

	// Descriptor 0 is the font atlas (ImGui's DX12 backend owns it); 1..N are textures handed out below.
	constexpr UINT             kSrvCount = 512;
	UINT                       g_srvStep = 0;
	std::vector<UINT>          g_freeSlots;
	struct Texture
	{
		ID3D12Resource* resource = nullptr;
		UINT            slot = 0;
	};
	std::unordered_map<std::uint64_t, Texture> g_textures;   // GPU handle ptr -> texture

	ID3D12CommandAllocator*    g_uploadAllocator = nullptr;
	ID3D12GraphicsCommandList* g_uploadList = nullptr;

	struct Frame
	{
		ID3D12CommandAllocator*     allocator = nullptr;
		ID3D12Resource*             backBuffer = nullptr;
		D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
		UINT64                      fence = 0;
	};
	std::vector<Frame> g_frames;

	// ---- originals ------------------------------------------------------------------------------------------
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

	// ---- vtable hooks (MinHook on the function the vtable slot points at; shared by every instance) --------------
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
			logger::error("hook {}: vtable slot {} is empty", a_what, a_index);
			return false;
		}
		if (MH_CreateHook(target, a_detour, reinterpret_cast<void**>(&a_original)) != MH_OK ||
			MH_EnableHook(target) != MH_OK) {
			logger::error("hook {}: MinHook failed at {}", a_what, target);
			a_original = nullptr;
			return false;
		}
		logger::info("hooked {} at {}", a_what, target);
		return true;
	}

	// ---- GPU sync -------------------------------------------------------------------------------------------------
	void WaitFor(UINT64 a_value, DWORD a_ms)
	{
		if (g_fence && g_fence->GetCompletedValue() < a_value) {
			g_fence->SetEventOnCompletion(a_value, g_fenceEvent);
			WaitForSingleObject(g_fenceEvent, a_ms);
		}
	}

	void WaitIdle()
	{
		std::scoped_lock l(g_gpuLock);
		if (g_queue && g_fence && g_fenceEvent) {
			const UINT64 v = ++g_fenceValue;
			if (SUCCEEDED(g_queue->Signal(g_fence, v))) {
				WaitFor(v, 2000);
			}
		}
	}

	// ---- render resources -----------------------------------------------------------------------------------------
	void ReleaseBackBuffers()
	{
		for (auto& f : g_frames) {
			Release(f.backBuffer);
		}
	}

	bool CreateBackBuffers(IDXGISwapChain* a_swapChain)
	{
		const UINT step = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE h = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
		for (UINT i = 0; i < g_frames.size(); ++i) {
			auto& f = g_frames[i];
			if (FAILED(a_swapChain->GetBuffer(i, IID_PPV_ARGS(&f.backBuffer)))) {
				logger::error("GetBuffer({}) failed", i);
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
			logger::error("no command queue was captured at swap-chain creation; the overlay stays off");
			g_failed = true;
			return false;
		}
		DXGI_SWAP_CHAIN_DESC desc{};
		if (FAILED(a_swapChain->GetDesc(&desc)) || FAILED(a_swapChain->GetDevice(IID_PPV_ARGS(&g_device)))) {
			logger::error("swap chain: GetDesc/GetDevice failed");
			g_failed = true;
			return false;
		}
		g_hwnd = desc.OutputWindow;
		g_format = desc.BufferDesc.Format;
		g_width = desc.BufferDesc.Width;
		g_height = desc.BufferDesc.Height;
		g_frames.assign(desc.BufferCount, {});
		logger::info("swap chain {}: {}x{}, {} buffers, format {}, window {}", static_cast<void*>(a_swapChain),
			desc.BufferDesc.Width, desc.BufferDesc.Height, desc.BufferCount, static_cast<int>(g_format),
			static_cast<void*>(g_hwnd));

		D3D12_DESCRIPTOR_HEAP_DESC rtv{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV, desc.BufferCount, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
		D3D12_DESCRIPTOR_HEAP_DESC srv{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvCount, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
		if (FAILED(g_device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&g_rtvHeap))) ||
			FAILED(g_device->CreateDescriptorHeap(&srv, IID_PPV_ARGS(&g_srvHeap)))) {
			logger::error("descriptor heaps failed");
			g_failed = true;
			return false;
		}
		g_srvStep = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		for (UINT i = kSrvCount - 1; i >= 1; --i) {
			g_freeSlots.push_back(i);
		}
		for (auto& f : g_frames) {
			if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.allocator)))) {
				logger::error("command allocator failed");
				g_failed = true;
				return false;
			}
		}
		if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frames[0].allocator, nullptr, IID_PPV_ARGS(&g_list))) ||
			FAILED(g_list->Close()) ||
			FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)))) {
			logger::error("command list / fence failed");
			g_failed = true;
			return false;
		}
		g_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!CreateBackBuffers(a_swapChain)) {
			g_failed = true;
			return false;
		}
		g_swapChain = a_swapChain;
		g_ready = true;   // gfx:: is usable from here on - the renderer uploads its textures during OnDeviceReady

		if (!renderer::OnDeviceReady(g_hwnd, g_width, g_height)) {
			logger::error("the renderer did not come up; the overlay stays off");
			g_ready = false;
			g_failed = true;
			return false;
		}
		g_origWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc)));
		// Every OBSE plugin has loaded by the first Present, so TestBench (which sorts after us) is there to register with.
		devbenchtool::Init(true);
		logger::info("overlay ready (Dear ImGui {}, {} frames in flight, {} texture descriptors)", IMGUI_VERSION, g_frames.size(), kSrvCount - 1);
		return true;
	}

	void Render(IDXGISwapChain* a_swapChain)
	{
		if (!Init(a_swapChain) || a_swapChain != g_swapChain) {
			return;
		}

		// The whole frame: input, the framework window, every consumer window and HUD element, ImGui::Render.
		renderer::OnFrame();

		ImDrawData* dd = ImGui::GetDrawData();
		if (!dd || dd->CmdListsCount == 0) {
			return;   // nothing on screen this frame (menu closed, no HUD element drew): nothing recorded
		}

		IDXGISwapChain3* sc3 = nullptr;
		if (FAILED(a_swapChain->QueryInterface(IID_PPV_ARGS(&sc3)))) {
			return;
		}
		const UINT idx = sc3->GetCurrentBackBufferIndex();
		sc3->Release();
		if (idx >= g_frames.size() || !g_frames[idx].backBuffer) {
			return;
		}

		std::scoped_lock l(g_gpuLock);
		Frame& f = g_frames[idx];
		WaitFor(f.fence, 1000);
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
		ImGui_ImplDX12_RenderDrawData(dd, g_list);
		std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
		g_list->ResourceBarrier(1, &b);
		g_list->Close();
		ID3D12CommandList* lists[] = { g_list };
		g_queue->ExecuteCommandLists(1, lists);
		f.fence = ++g_fenceValue;
		g_queue->Signal(g_fence, f.fence);
	}

	// ---- detours ----------------------------------------------------------------------------------------------------
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
		}
		const HRESULT hr = o_ResizeBuffers(a_this, a_count, a_w, a_h, a_fmt, a_flags);
		if (ours && SUCCEEDED(hr)) {
			DXGI_SWAP_CHAIN_DESC desc{};
			a_this->GetDesc(&desc);
			if (desc.BufferCount != g_frames.size()) {
				logger::warn("buffer count changed {} -> {}; the overlay stays off until restart", g_frames.size(), desc.BufferCount);
				g_ready = false;
				g_failed = true;
				return hr;
			}
			CreateBackBuffers(a_this);
			g_width = desc.BufferDesc.Width;
			g_height = desc.BufferDesc.Height;
			logger::info("resized to {}x{}", desc.BufferDesc.Width, desc.BufferDesc.Height);
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
			logger::info("captured the presenting command queue {} via {}", static_cast<void*>(q), a_via);
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

	// ---- input ------------------------------------------------------------------------------------------------------
	// Every message goes to the input module first; it decides what the framework takes (the toggle key, and
	// everything while the menu is up) and what the game still gets (see Input.cpp).
	LRESULT CALLBACK WndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wp, LPARAM a_lp)
	{
		if (g_ready && input::OnWindowMessage(a_hwnd, a_msg, a_wp, a_lp)) {
			if (a_msg == WM_INPUT) {
				return DefWindowProcW(a_hwnd, a_msg, a_wp, a_lp);   // the raw-input buffer must still be released
			}
			return 0;
		}
		return CallWindowProcW(g_origWndProc, a_hwnd, a_msg, a_wp, a_lp);
	}
}

namespace Overlay
{
	bool Install()
	{
		if (const auto st = MH_Initialize(); st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
			logger::error("MinHook failed to initialise ({})", static_cast<int>(st));
			return false;
		}
		HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
		if (!dxgi) {
			logger::error("dxgi.dll did not load");
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
				logger::info("hooked dxgi!{}", e.name);
				++ok;
			} else {
				logger::warn("could not hook dxgi!{}", e.name);
			}
		}
		return ok > 0;
	}

	void* GameWindow() { return g_hwnd; }
}

namespace gfx
{
	bool Ready() { return g_ready.load() && g_device && g_queue && g_srvHeap; }

	bool InitImGuiBackend()
	{
		if (!g_device || !g_srvHeap) {
			return false;
		}
		g_backendUp = ImGui_ImplDX12_Init(g_device, static_cast<int>(g_frames.size()), g_format, g_srvHeap,
			g_srvHeap->GetCPUDescriptorHandleForHeapStart(), g_srvHeap->GetGPUDescriptorHandleForHeapStart());
		return g_backendUp;
	}

	void NewFrame()
	{
		if (g_backendUp) {
			ImGui_ImplDX12_NewFrame();
		}
	}

	void InvalidateDeviceObjects()
	{
		if (!g_backendUp) {
			return;
		}
		// The font texture may still be read by a frame in flight; releasing it under the GPU is a device removal.
		WaitIdle();
		ImGui_ImplDX12_InvalidateDeviceObjects();
	}

	void* CreateTextureRGBA(const void* a_rgba, int a_width, int a_height)
	{
		if (!Ready() || !a_rgba || a_width <= 0 || a_height <= 0) {
			return nullptr;
		}
		std::scoped_lock l(g_gpuLock);
		if (g_freeSlots.empty()) {
			logger::warn("texture: all {} descriptors are in use; nothing uploaded", kSrvCount - 1);
			return nullptr;
		}

		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC rd{};
		rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		rd.Width = static_cast<UINT64>(a_width);
		rd.Height = static_cast<UINT>(a_height);
		rd.DepthOrArraySize = 1;
		rd.MipLevels = 1;
		rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rd.SampleDesc.Count = 1;
		rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		ID3D12Resource* tex = nullptr;
		if (FAILED(g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)))) {
			logger::warn("texture: CreateCommittedResource {}x{} failed", a_width, a_height);
			return nullptr;
		}

		const UINT pitch = (static_cast<UINT>(a_width) * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
		const UINT size = pitch * static_cast<UINT>(a_height);
		D3D12_HEAP_PROPERTIES up{};
		up.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC bd{};
		bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bd.Width = size;
		bd.Height = 1;
		bd.DepthOrArraySize = 1;
		bd.MipLevels = 1;
		bd.SampleDesc.Count = 1;
		bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ID3D12Resource* upload = nullptr;
		if (FAILED(g_device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)))) {
			logger::warn("texture: upload buffer of {} bytes failed", size);
			tex->Release();
			return nullptr;
		}
		void*       mapped = nullptr;
		D3D12_RANGE none{ 0, 0 };
		if (FAILED(upload->Map(0, &none, &mapped)) || !mapped) {
			upload->Release();
			tex->Release();
			return nullptr;
		}
		for (int y = 0; y < a_height; ++y) {
			std::memcpy(static_cast<std::uint8_t*>(mapped) + static_cast<std::size_t>(y) * pitch,
				static_cast<const std::uint8_t*>(a_rgba) + static_cast<std::size_t>(y) * a_width * 4, static_cast<std::size_t>(a_width) * 4);
		}
		upload->Unmap(0, nullptr);

		if (!g_uploadAllocator &&
			(FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_uploadAllocator))) ||
				FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_uploadAllocator, nullptr, IID_PPV_ARGS(&g_uploadList))) ||
				FAILED(g_uploadList->Close()))) {
			logger::warn("texture: the upload command list could not be created");
			Release(g_uploadAllocator);
			upload->Release();
			tex->Release();
			return nullptr;
		}
		g_uploadAllocator->Reset();
		g_uploadList->Reset(g_uploadAllocator, nullptr);
		D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
		src.pResource = upload;
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint.Footprint = { DXGI_FORMAT_R8G8B8A8_UNORM, static_cast<UINT>(a_width), static_cast<UINT>(a_height), 1, pitch };
		dst.pResource = tex;
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;
		g_uploadList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		D3D12_RESOURCE_BARRIER b{};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = tex;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		g_uploadList->ResourceBarrier(1, &b);
		g_uploadList->Close();
		ID3D12CommandList* lists[] = { g_uploadList };
		g_queue->ExecuteCommandLists(1, lists);
		const UINT64 v = ++g_fenceValue;
		g_queue->Signal(g_fence, v);
		WaitFor(v, 2000);
		upload->Release();

		const UINT slot = g_freeSlots.back();
		g_freeSlots.pop_back();
		D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
		sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		sv.Texture2D.MipLevels = 1;
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE gpu = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
		cpu.ptr += static_cast<SIZE_T>(slot) * g_srvStep;
		gpu.ptr += static_cast<UINT64>(slot) * g_srvStep;
		g_device->CreateShaderResourceView(tex, &sv, cpu);
		g_textures[gpu.ptr] = Texture{ tex, slot };
		logger::debug("texture: {}x{} uploaded to descriptor {}", a_width, a_height, slot);
		return reinterpret_cast<void*>(gpu.ptr);
	}

	void ReleaseTexture(void* a_textureId)
	{
		if (!a_textureId) {
			return;
		}
		WaitIdle();
		std::scoped_lock l(g_gpuLock);
		if (const auto it = g_textures.find(reinterpret_cast<std::uint64_t>(a_textureId)); it != g_textures.end()) {
			it->second.resource->Release();
			g_freeSlots.push_back(it->second.slot);
			g_textures.erase(it);
		}
	}

	bool DecodeImageFile(const std::wstring& a_path, std::vector<std::uint8_t>& a_rgba, int& a_width, int& a_height)
	{
		const HRESULT          co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // WIC needs COM on this thread
		IWICImagingFactory*    factory = nullptr;
		IWICBitmapDecoder*     decoder = nullptr;
		IWICBitmapFrameDecode* frame = nullptr;
		IWICFormatConverter*   converter = nullptr;
		bool                   ok = false;
		UINT                   w = 0, h = 0;
		if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
			SUCCEEDED(factory->CreateDecoderFromFilename(a_path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
			SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w > 0 && h > 0 &&
			SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
			SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
			a_rgba.resize(static_cast<std::size_t>(w) * h * 4);
			ok = SUCCEEDED(converter->CopyPixels(nullptr, w * 4u, static_cast<UINT>(a_rgba.size()), a_rgba.data()));
		}
		Release(converter);
		Release(frame);
		Release(decoder);
		Release(factory);
		if (SUCCEEDED(co)) {
			CoUninitialize();
		}
		a_width = static_cast<int>(w);
		a_height = static_cast<int>(h);
		return ok;
	}

	void GetBackBufferSize(unsigned& a_width, unsigned& a_height)
	{
		a_width = g_width;
		a_height = g_height;
	}
}
