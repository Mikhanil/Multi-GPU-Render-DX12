#include "Texture.h"
#include "DirectXTex.h"
#include "filesystem"
#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"
#include "ResourceUploadBatch.h"
#include <winrt/base.h>

#include "d3dApp.h"
#include "GDataUploader.h"
#include "GMemory.h"
#include "GResourceStateTracker.h"

UINT Texture::textureIndexGlobal = 0;

TextureUsage Texture::GetTextureType() const
{
	return usage;
}

UINT Texture::GetTextureIndex() const
{
	return textureIndex;
}

Texture::Texture(std::wstring name, std::wstring filename, TextureUsage use): GResource( name),
                                                                             Filename(std::move(filename)), usage(use)
{
	textureIndex = textureIndexGlobal++;
}

Texture::Texture(const D3D12_RESOURCE_DESC& resourceDesc, const D3D12_CLEAR_VALUE* clearValue,
                 TextureUsage textureUsage, const std::wstring& name) : GResource(resourceDesc, clearValue, name),
                                                                        usage(textureUsage)
{
	textureIndex = textureIndexGlobal++;
	CreateViews();
}

Texture::Texture(ComPtr<ID3D12Resource> resource, TextureUsage textureUsage,
                 const std::wstring& name) : GResource(resource, name), usage(textureUsage)
{
	textureIndex = textureIndexGlobal++;
	CreateViews();
}

Texture::Texture(const Texture& copy) : GResource(copy), textureIndex(copy.textureIndex)
{
	CreateViews();
}

Texture::Texture(Texture&& copy) : GResource(copy), textureIndex(std::move(copy.textureIndex))
{
	CreateViews();
}

Texture& Texture::operator=(const Texture& other)
{
	GResource::operator=(other);

	textureIndex = other.textureIndex;

	CreateViews();

	return *this;
}

Texture& Texture::operator=(Texture&& other)
{
	GResource::operator=(other);

	textureIndex = other.textureIndex;

	CreateViews();

	return *this;
}

void Texture::CreateViews()
{
	if (dxResource)
	{
		auto& app = DXLib::D3DApp::GetApp();
		auto& device = app.GetDevice();

		CD3DX12_RESOURCE_DESC desc(dxResource->GetDesc());

		if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0 &&
			CheckRTVSupport())
		{
			m_RenderTargetView = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			device.CreateRenderTargetView(dxResource.Get(), nullptr, m_RenderTargetView.GetCPUHandle());
		}
		if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0 &&
			CheckDSVSupport())
		{
			m_DepthStencilView = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
			device.CreateDepthStencilView(dxResource.Get(), nullptr, m_DepthStencilView.GetCPUHandle());
		}
	}

	std::lock_guard<std::mutex> lock(m_ShaderResourceViewsMutex);
	std::lock_guard<std::mutex> guard(m_UnorderedAccessViewsMutex);

	// SRVs and UAVs will be created as needed.
	m_ShaderResourceViews.clear();
	m_UnorderedAccessViews.clear();
}

GMemory Texture::CreateShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) const
{
	auto& app = DXLib::D3DApp::GetApp();
	auto& device = app.GetDevice();

	auto srv = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	device.CreateShaderResourceView(dxResource.Get(), srvDesc, srv.GetCPUHandle());

	return srv;
}

GMemory Texture::CreateUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc) const
{
	auto& app = DXLib::D3DApp::GetApp();
	auto& device = app.GetDevice();

	auto uav = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	device.CreateUnorderedAccessView(dxResource.Get(), nullptr, uavDesc, uav.GetCPUHandle());

	return uav;
}


Texture::~Texture()
{
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) const
{
	std::size_t hash = 0;
	if (srvDesc)
	{
		hash = std::hash<D3D12_SHADER_RESOURCE_VIEW_DESC>{}(*srvDesc);
	}

	std::lock_guard<std::mutex> lock(m_ShaderResourceViewsMutex);

	auto iter = m_ShaderResourceViews.find(hash);
	if (iter == m_ShaderResourceViews.end())
	{
		auto srv = CreateShaderResourceView(srvDesc);
		iter = m_ShaderResourceViews.insert({hash, std::move(srv)}).first;
	}

	return iter->second.GetCPUHandle();
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc) const
{
	std::size_t hash = 0;
	if (uavDesc)
	{
		hash = std::hash<D3D12_UNORDERED_ACCESS_VIEW_DESC>{}(*uavDesc);
	}

	std::lock_guard<std::mutex> guard(m_UnorderedAccessViewsMutex);

	auto iter = m_UnorderedAccessViews.find(hash);
	if (iter == m_UnorderedAccessViews.end())
	{
		auto uav = CreateUnorderedAccessView(uavDesc);
		iter = m_UnorderedAccessViews.insert({hash, std::move(uav)}).first;
	}

	return iter->second.GetCPUHandle();
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetRenderTargetView() const
{
	return m_RenderTargetView.GetCPUHandle();
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetDepthStencilView() const
{
	return m_DepthStencilView.GetCPUHandle();
}


DXGI_FORMAT Texture::GetUAVCompatableFormat(DXGI_FORMAT format)
{
	DXGI_FORMAT uavFormat = format;

	switch (format)
	{
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_TYPELESS:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		uavFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		break;
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
		uavFormat = DXGI_FORMAT_R32_FLOAT;
		break;
	}

	return uavFormat;
}

void Texture::LoadTexture(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* commandList)
{
	if (isLoaded) return;


	std::filesystem::path filePath(Filename);
	if (!exists(filePath))
	{
		assert("File not found.");
	}


	//winrt::init_apartment();

	//DirectX::ResourceUploadBatch upload(device);
	//upload.Begin();
	//
	//if (filePath.extension() == ".dds")
	//{

	//	ThrowIfFailed(DirectX::CreateDDSTextureFromFile(device,
	//		upload, Filename.c_str(), directxResource.GetAddressOf()));
	//}
	//else
	//{
	//	DirectX::CreateWICTextureFromFile(device, upload, Filename.c_str(), directxResource.GetAddressOf(), true);
	//}
	//

	//// Upload the resources to the GPU.
	//upload.End(queue).wait();

	//isLoaded = true;
	//return;

	DirectX::TexMetadata metadata;
	DirectX::ScratchImage scratchImage;


	UINT resFlags = D3D12_RESOURCE_FLAG_NONE;

	if (filePath.extension() == ".dds")
	{
		ThrowIfFailed(DirectX::LoadFromDDSFile(Filename.c_str(), DirectX::DDS_FLAGS_NONE , &metadata, scratchImage));
	}
	else if (filePath.extension() == ".hdr")
	{
		ThrowIfFailed(DirectX::LoadFromHDRFile(Filename.c_str(), &metadata, scratchImage));
	}
	else if (filePath.extension() == ".tga")
	{
		ThrowIfFailed(DirectX::LoadFromTGAFile(Filename.c_str(), &metadata, scratchImage));
	}
	else
	{
		ThrowIfFailed(
			DirectX::LoadFromWICFile(Filename.c_str(), DirectX::WIC_FLAGS_FORCE_RGB, &metadata, scratchImage));

		//Если это не DDS или "специфичная" текстура, то для нее нужно будет генерировать мипмапы
		//по этому даем возможность сделать ее UAV
		resFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}

	/*Пока выключил гамма корекцию.*/
	if (usage == TextureUsage::Albedo && filePath.extension() != ".dds")
	{
		//metadata.format = MakeSRGB(metadata.format);
	}

	D3D12_RESOURCE_DESC desc = {};
	desc.Width = static_cast<UINT>(metadata.width);
	desc.Height = static_cast<UINT>(metadata.height);
	/*
	 * DDS текстуры нельзя использовать как UAV для генерации мипмап карт.
	 */
	desc.MipLevels = resFlags == D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		                 ? 0
		                 : static_cast<UINT16>(metadata.mipLevels);
	desc.DepthOrArraySize = (metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE3D)
		                        ? static_cast<UINT16>(metadata.depth)
		                        : static_cast<UINT16>(metadata.arraySize);
	desc.Format = metadata.format;
	desc.Flags = static_cast<D3D12_RESOURCE_FLAGS>(resFlags);
	desc.SampleDesc.Count = 1;
	desc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);

	ThrowIfFailed(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&dxResource)));

	// Update the global state tracker.
	GResourceStateTracker::AddGlobalResourceState(dxResource.Get(), D3D12_RESOURCE_STATE_COMMON);

	
	std::vector<D3D12_SUBRESOURCE_DATA> subresources(scratchImage.GetImageCount());
	ThrowIfFailed(
		PrepareUpload(device, scratchImage.GetImages(), scratchImage.GetImageCount(), scratchImage.GetMetadata(),
			subresources));

	if (dxResource)
	{
		
		
		commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(dxResource.Get(),
		                                                                      D3D12_RESOURCE_STATE_COMMON,
		                                                                      D3D12_RESOURCE_STATE_COPY_DEST));

		GResourceStateTracker::AddGlobalResourceState(dxResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
		
		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(dxResource.Get(),
		                                                            0, static_cast<unsigned int>(subresources.size()));
		
		auto upload = DXAllocator::UploadData(uploadBufferSize, nullptr, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
				


		UpdateSubresources(commandList,
			dxResource.Get(), &upload.d3d12Resource,
			upload.Offset, 0, static_cast<unsigned int>(subresources.size()),
		                   subresources.data());

		track.push_back(dxResource);


		commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(dxResource.Get(),
		                                                                      D3D12_RESOURCE_STATE_COPY_DEST,
		                                                                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

		GResourceStateTracker::AddGlobalResourceState(dxResource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	isLoaded = true;
}

void Texture::ClearTrack()
{
	track.clear();
}

bool Texture::IsUAVCompatibleFormat(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32A32_SINT:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R16G16B16A16_SINT:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SINT:
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_R32_SINT:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_SINT:
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_SINT:
		return true;
	default:
		return false;
	}
}

bool Texture::IsSRGBFormat(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		return true;
	default:
		return false;
	}
}

bool Texture::IsBGRFormat(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		return true;
	default:
		return false;
	}
}

bool Texture::IsDepthFormat(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_D16_UNORM:
		return true;
	default:
		return false;
	}
}

DXGI_FORMAT Texture::GetTypelessFormat(DXGI_FORMAT format)
{
	DXGI_FORMAT typelessFormat = format;

	switch (format)
	{
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32A32_SINT:
		typelessFormat = DXGI_FORMAT_R32G32B32A32_TYPELESS;
		break;
	case DXGI_FORMAT_R32G32B32_FLOAT:
	case DXGI_FORMAT_R32G32B32_UINT:
	case DXGI_FORMAT_R32G32B32_SINT:
		typelessFormat = DXGI_FORMAT_R32G32B32_TYPELESS;
		break;
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R16G16B16A16_SNORM:
	case DXGI_FORMAT_R16G16B16A16_SINT:
		typelessFormat = DXGI_FORMAT_R16G16B16A16_TYPELESS;
		break;
	case DXGI_FORMAT_R32G32_FLOAT:
	case DXGI_FORMAT_R32G32_UINT:
	case DXGI_FORMAT_R32G32_SINT:
		typelessFormat = DXGI_FORMAT_R32G32_TYPELESS;
		break;
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		typelessFormat = DXGI_FORMAT_R32G8X24_TYPELESS;
		break;
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_UINT:
		typelessFormat = DXGI_FORMAT_R10G10B10A2_TYPELESS;
		break;
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R8G8B8A8_SINT:
		typelessFormat = DXGI_FORMAT_R8G8B8A8_TYPELESS;
		break;
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R16G16_SNORM:
	case DXGI_FORMAT_R16G16_SINT:
		typelessFormat = DXGI_FORMAT_R16G16_TYPELESS;
		break;
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_R32_SINT:
		typelessFormat = DXGI_FORMAT_R32_TYPELESS;
		break;
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R8G8_SNORM:
	case DXGI_FORMAT_R8G8_SINT:
		typelessFormat = DXGI_FORMAT_R8G8_TYPELESS;
		break;
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_SNORM:
	case DXGI_FORMAT_R16_SINT:
		typelessFormat = DXGI_FORMAT_R16_TYPELESS;
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_SNORM:
	case DXGI_FORMAT_R8_SINT:
		typelessFormat = DXGI_FORMAT_R8_TYPELESS;
		break;
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
		typelessFormat = DXGI_FORMAT_BC1_TYPELESS;
		break;
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC2_UNORM_SRGB:
		typelessFormat = DXGI_FORMAT_BC2_TYPELESS;
		break;
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC3_UNORM_SRGB:
		typelessFormat = DXGI_FORMAT_BC3_TYPELESS;
		break;
	case DXGI_FORMAT_BC4_UNORM:
	case DXGI_FORMAT_BC4_SNORM:
		typelessFormat = DXGI_FORMAT_BC4_TYPELESS;
		break;
	case DXGI_FORMAT_BC5_UNORM:
	case DXGI_FORMAT_BC5_SNORM:
		typelessFormat = DXGI_FORMAT_BC5_TYPELESS;
		break;
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		typelessFormat = DXGI_FORMAT_B8G8R8A8_TYPELESS;
		break;
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		typelessFormat = DXGI_FORMAT_B8G8R8X8_TYPELESS;
		break;
	case DXGI_FORMAT_BC6H_UF16:
	case DXGI_FORMAT_BC6H_SF16:
		typelessFormat = DXGI_FORMAT_BC6H_TYPELESS;
		break;
	case DXGI_FORMAT_BC7_UNORM:
	case DXGI_FORMAT_BC7_UNORM_SRGB:
		typelessFormat = DXGI_FORMAT_BC7_TYPELESS;
		break;
	}

	return typelessFormat;
}


ID3D12Resource* Texture::GetResource() const
{
	if (isLoaded)return dxResource.Get();

	return nullptr;
}

std::wstring& Texture::GetName()
{
	return resourceName;
}

std::wstring& Texture::GetFileName()
{
	return Filename;
}
