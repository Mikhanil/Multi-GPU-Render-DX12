#pragma once
#include <mutex>

#include "d3dUtil.h"
#include "GCommandQueue.h"
#include "ComputePSO.h"
#include "GMemory.h"
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


class Texture : public GResource
{
	static UINT textureIndexGlobal;
	UINT textureIndex = -1;

	TextureUsage usage;

	custom_vector<ComPtr<ID3D12Resource>> track = DXAllocator::CreateVector<ComPtr<ID3D12Resource>>();


	mutable std::mutex shaderResourceViewsMutex;
	mutable std::mutex unorderedAccessViewsMutex;
	

public:
	bool HasMipMap;

	static void Resize(Texture& texture, uint32_t width, uint32_t height, uint32_t depthOrArraySize);


	static void GenerateMipMaps(std::shared_ptr<GCommandList> cmdList, Texture** textures, size_t count);
	TextureUsage GetTextureType() const;

	UINT GetTextureIndex() const;

	Texture(std::wstring name = L"", TextureUsage use = TextureUsage::Diffuse);

	Texture(const D3D12_RESOURCE_DESC& resourceDesc,
		const std::wstring& name = L"", TextureUsage textureUsage = TextureUsage::Albedo,
		const D3D12_CLEAR_VALUE* clearValue = nullptr);
	Texture(ComPtr<ID3D12Resource> resource,
	                 TextureUsage textureUsage = TextureUsage::Albedo,
	                 const std::wstring& name = L"");

	Texture(const Texture& copy);
	Texture(Texture&& copy);

	Texture& operator=(const Texture& other);
	Texture& operator=(Texture&& other);
	
	void CreateShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc, GMemory* memory, size_t offset = 0) const;
	void CreateUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc, GMemory* memory, size_t offset = 0) const;
	void CreateRenderTargetView(const D3D12_RENDER_TARGET_VIEW_DESC* rtvDesc, GMemory* memory, size_t offset = 0) const;
	void CreateDepthStencilView(const D3D12_DEPTH_STENCIL_VIEW_DESC* dsvDesc, GMemory* memory, size_t offset = 0) const;
	
	virtual ~Texture();


	static std::shared_ptr<Texture> LoadTextureFromFile(std::wstring filepath,
	                                                    std::shared_ptr<GCommandList> commandList, TextureUsage usage = TextureUsage::Diffuse);

	void ClearTrack();

	ID3D12Resource* GetResource() const;

	std::wstring& GetName();

	static DXGI_FORMAT GetUAVCompatableFormat(DXGI_FORMAT format);
	
	static bool IsUAVCompatibleFormat(DXGI_FORMAT format);

	static bool IsSRGBFormat(DXGI_FORMAT format);

	static bool IsBGRFormat(DXGI_FORMAT format);

	static bool IsDepthFormat(DXGI_FORMAT format);

	static DXGI_FORMAT GetTypelessFormat(DXGI_FORMAT format);
};
