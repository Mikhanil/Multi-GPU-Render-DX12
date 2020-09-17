#pragma once
#include <mutex>

#include "d3dUtil.h"
#include "GCommandQueue.h"
#include "ComputePSO.h"
#include "GResource.h"

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


class GTexture : public GResource
{
	static UINT textureIndexGlobal;
	UINT textureIndex = -1;

	TextureUsage usage;

	custom_vector<ComPtr<ID3D12Resource>> track = DXAllocator::CreateVector<ComPtr<ID3D12Resource>>();


	mutable std::mutex shaderResourceViewsMutex;
	mutable std::mutex unorderedAccessViewsMutex;
	

public:
	bool HasMipMap;

	static void Resize(GTexture& texture, uint32_t width, uint32_t height, uint32_t depthOrArraySize);


	static void GenerateMipMaps(std::shared_ptr<GCommandList> cmdList, GTexture** textures, size_t count);
	TextureUsage GetTextureType() const;

	UINT GetTextureIndex() const;

	GTexture(std::wstring name = L"", TextureUsage use = TextureUsage::Diffuse);

	GTexture(const D3D12_RESOURCE_DESC& resourceDesc,
		const std::wstring& name = L"", TextureUsage textureUsage = TextureUsage::Albedo,
		const D3D12_CLEAR_VALUE* clearValue = nullptr);
	GTexture(ComPtr<ID3D12Resource> resource,
	                 TextureUsage textureUsage = TextureUsage::Albedo,
	                 const std::wstring& name = L"");

	GTexture(const GTexture& copy);
	GTexture(GTexture&& copy);

	GTexture& operator=(const GTexture& other);
	GTexture& operator=(GTexture&& other);
	
	
	
	virtual ~GTexture();


	static std::shared_ptr<GTexture> LoadTextureFromFile(std::wstring filepath,
	                                                    std::shared_ptr<GCommandList> commandList, TextureUsage usage = TextureUsage::Diffuse);

	void ClearTrack();


	std::wstring& GetName();

	static DXGI_FORMAT GetUAVCompatableFormat(DXGI_FORMAT format);
	
	static bool IsUAVCompatibleFormat(DXGI_FORMAT format);

	static bool IsSRGBFormat(DXGI_FORMAT format);

	static bool IsBGRFormat(DXGI_FORMAT format);

	static bool IsDepthFormat(DXGI_FORMAT format);

	static DXGI_FORMAT GetTypelessFormat(DXGI_FORMAT format);
};
