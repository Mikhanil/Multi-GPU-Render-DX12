#pragma once
#include <mutex>

#include "d3dUtil.h"
#include "GeneratedMipsPSO.h"
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

	std::wstring Filename;
	bool isLoaded = false;

	TextureUsage usage;

	custom_vector<ComPtr<ID3D12Resource>> track = DXAllocator::CreateVector<ComPtr<ID3D12Resource>>();

	GMemory m_RenderTargetView;
	GMemory m_DepthStencilView;

	mutable custom_unordered_map<size_t, GMemory> m_ShaderResourceViews = DXAllocator::CreateUnorderedMap<
		size_t, GMemory>();
	mutable custom_unordered_map<size_t, GMemory> m_UnorderedAccessViews = DXAllocator::CreateUnorderedMap<
		size_t, GMemory>();

	mutable std::mutex m_ShaderResourceViewsMutex;
	mutable std::mutex m_UnorderedAccessViewsMutex;

public:

	bool CheckSRVSupport() const
	{
		return CheckFormatSupport(D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE);
	}

	bool CheckRTVSupport() const
	{
		return CheckFormatSupport(D3D12_FORMAT_SUPPORT1_RENDER_TARGET);
	}

	bool CheckUAVSupport() const
	{
		return CheckFormatSupport(D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) &&
			CheckFormatSupport(D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) &&
			CheckFormatSupport(D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
	}

	bool CheckDSVSupport() const
	{
		return CheckFormatSupport(D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL);
	}

	TextureUsage GetTextureType() const;

	UINT GetTextureIndex() const;

	Texture(std::wstring name, std::wstring filename, TextureUsage use = TextureUsage::Diffuse);

	Texture(const D3D12_RESOURCE_DESC& resourceDesc,
	                 const D3D12_CLEAR_VALUE* clearValue = nullptr,
	                 TextureUsage textureUsage = TextureUsage::Albedo,
	                 const std::wstring& name = L"");
	Texture(ComPtr<ID3D12Resource> resource,
	                 TextureUsage textureUsage = TextureUsage::Albedo,
	                 const std::wstring& name = L"");

	Texture(const Texture& copy);
	Texture(Texture&& copy);

	Texture& operator=(const Texture& other);
	Texture& operator=(Texture&& other);
	void CreateViews();
	GMemory CreateShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) const;
	GMemory CreateUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc) const;

	virtual ~Texture();


	static DXGI_FORMAT GetUAVCompatableFormat(DXGI_FORMAT format);

	void LoadTexture(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* commandList);

	void ClearTrack();

	ID3D12Resource* GetResource() const;

	std::wstring& GetName();

	std::wstring& GetFileName();


	bool IsUAVCompatibleFormat(DXGI_FORMAT format);

	bool IsSRGBFormat(DXGI_FORMAT format);

	bool IsBGRFormat(DXGI_FORMAT format);

	bool IsDepthFormat(DXGI_FORMAT format);

	DXGI_FORMAT GetTypelessFormat(DXGI_FORMAT format);

	D3D12_CPU_DESCRIPTOR_HANDLE GetShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) const override;
	D3D12_CPU_DESCRIPTOR_HANDLE GetUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc) const override;
	D3D12_CPU_DESCRIPTOR_HANDLE GetRenderTargetView() const;
	D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const;
};
