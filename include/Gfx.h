#pragma once

// The Direct3D 12 half of the framework, owned by the overlay (Overlay.cpp). Skyrim AMF drew on the game's D3D11
// device and let DirectXTK and ImGui's DX11 backend create textures on it directly; Oblivion Remastered is DX12-only,
// where a texture is a resource plus a descriptor in a shader-visible heap plus a copy on the GPU queue. Everything
// that needs a texture (the knotwork frame, theme art, a consumer's LoadTexture) comes through here, so the D3D12
// details stay in one file.
//
// An ImTextureID here is a D3D12_GPU_DESCRIPTOR_HANDLE's ptr - what ImGui's DX12 backend binds for a draw command.

namespace gfx
{
	// True once the device, the presenting queue and the descriptor heap exist.
	bool Ready();

	// Starts ImGui's DX12 renderer backend on the game's device (the renderer calls it at device-ready, after
	// ImGui::CreateContext and the Win32 backend).
	bool InitImGuiBackend();

	// Per frame, before ImGui::NewFrame: recreates the backend's device objects (the font texture) when missing.
	void NewFrame();

	// Waits for the GPU to finish every frame in flight, then drops the backend's device objects, so the next
	// NewFrame rebuilds the font texture from a new atlas. A font or text-size change calls this.
	void InvalidateDeviceObjects();

	// Uploads tightly packed RGBA8 pixels and returns the texture's ImTextureID, or null (logged).
	void* CreateTextureRGBA(const void* a_rgba, int a_width, int a_height);
	void  ReleaseTexture(void* a_textureId);

	// Decodes a PNG/JPG/BMP (anything WIC reads) to tightly packed RGBA8.
	bool DecodeImageFile(const std::wstring& a_path, std::vector<std::uint8_t>& a_rgba, int& a_width, int& a_height);

	// The swap chain's current size, in pixels.
	void GetBackBufferSize(unsigned& a_width, unsigned& a_height);
}
