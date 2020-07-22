#pragma once
#include "d3dUtil.h"
#include "GeneratedMipsPSO.h"

enum class TextureUsage
{
	Albedo,
	Diffuse = Albedo,
	// Treat Diffuse and Albedo textures the same.
	Heightmap,
	Depth = Heightmap,
	// Treat height and depth textures the same.
	Normalmap,
	RenderTarget,
	// Texture is used as a render target.
};


class Texture
{
	static UINT textureIndexGlobal;

	UINT textureIndex = -1;

	std::wstring Filename;
	std::string Name;
	ComPtr<ID3D12Resource> directxResource = nullptr;
	bool isLoaded = false;

	TextureUsage usage;

	custom_vector<ComPtr<ID3D12Resource>> track = DXAllocator::CreateVector<ComPtr<ID3D12Resource>>();

public:

	TextureUsage GetTextureType() const;

	UINT GetTextureIndex() const;

	Texture(std::string name, std::wstring filename, TextureUsage use = TextureUsage::Diffuse);
	static DXGI_FORMAT GetUAVCompatableFormat(DXGI_FORMAT format);

	void LoadTexture(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* commandList);

	void ClearTrack();

	ID3D12Resource* GetResource() const;

	std::string& GetName();

	std::wstring& GetFileName();


	bool IsUAVCompatibleFormat(DXGI_FORMAT format);

	bool IsSRGBFormat(DXGI_FORMAT format);

	bool IsBGRFormat(DXGI_FORMAT format);

	bool IsDepthFormat(DXGI_FORMAT format);

	DXGI_FORMAT GetTypelessFormat(DXGI_FORMAT format);
};
