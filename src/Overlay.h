#pragma once

// The DirectX 12 overlay: finds the game's swap chain and command queue as the game creates them, and draws
// Dear ImGui over every presented frame. Oblivion Remastered is DX12-only (-dx11 is refused), so there is no
// D3D11 path as in Skyrim AMF.
//
// How the queue is found (RESEARCH.md, the kacejot/dx12-imgui-overlay design): OBSE64 calls our Load before the
// game's WinMain, so the DXGI factory exports are hooked before the renderer exists; the factory's
// CreateSwapChainForHwnd / CreateSwapChain receive the ID3D12CommandQueue as their "device" argument, which is the
// queue that presents - the one ImGui's command list must be executed on.

namespace Overlay
{
	bool Install();          // hook the DXGI factory exports; call from OBSEPlugin_Load
	bool IsOpen();
	void SetOpen(bool a_open);
	void Toggle();
}
