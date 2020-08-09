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

void Texture::Resize(Texture& texture, uint32_t width, uint32_t height, uint32_t depthOrArraySize)
{
	if(texture.dxResource)
	{
		GResourceStateTracker::RemoveGlobalResourceState(texture.dxResource.Get());

		CD3DX12_RESOURCE_DESC resDesc(texture.dxResource->GetDesc());
		resDesc.Width = std::max(width, 1u);
		resDesc.Height = std::max(height, 1u);
		resDesc.DepthOrArraySize = depthOrArraySize;

		auto& device = DXLib::D3DApp::GetApp().GetDevice();

		ThrowIfFailed(device.CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&resDesc,
			D3D12_RESOURCE_STATE_COMMON,
			texture.clearValue.get(),
			IID_PPV_ARGS(&texture.dxResource)
		));

		texture.dxResource->SetName(texture.GetName().c_str());

		GResourceStateTracker::AddGlobalResourceState(texture.dxResource.Get(), D3D12_RESOURCE_STATE_COMMON);
	}
}

void Texture::GenerateMipMaps(DXLib::GCommandQueue* queue, Texture** textures, size_t count)
{
	UINT requiredHeapSize = 0;

	for (int i =0; i < count; ++i)
	{
		requiredHeapSize += textures[i]->GetResource()->GetDesc().MipLevels - 1;
	}

	if (requiredHeapSize == 0)
	{
		return;
	}

	
	auto& device = DXLib::D3DApp::GetApp().GetDevice();
	
	const auto mipMapsMemory = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2 * requiredHeapSize);
	UINT descriptorSize = device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


	D3D12_SHADER_RESOURCE_VIEW_DESC srcTextureSRVDesc = {};
	srcTextureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srcTextureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;


	D3D12_UNORDERED_ACCESS_VIEW_DESC destTextureUAVDesc = {};	
	destTextureUAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	
	GeneratedMipsPSO genMipMapPSO;

	auto cmdList = queue->GetCommandList();
	
	cmdList->SetComputeRootSignature(genMipMapPSO.GetRootSignature().Get());
	cmdList->SetPipelineState(genMipMapPSO.GetPipelineState().Get());

	auto dHeap = mipMapsMemory.GetDescriptorHeap();
	cmdList->SetDescriptorHeaps(1, &dHeap);


	CD3DX12_CPU_DESCRIPTOR_HANDLE currentCPUHandle(mipMapsMemory.GetCPUHandle(), 0,
		descriptorSize);

	CD3DX12_GPU_DESCRIPTOR_HANDLE currentGPUHandle(mipMapsMemory.GetGPUHandle(), 0,
		descriptorSize);


	/*Почему-то логика взаимодействия с константным буфером как для отрисовки тут не работает*/
	//auto mipBuffer = genMipMapPSO->GetBuffer();

	for (int i = 0; i < count; ++i)
	{
		auto tex = textures[i];
		
		auto texture = tex->GetResource();
		auto textureDesc = texture->GetDesc();

		for (uint32_t TopMip = 0; TopMip < textureDesc.MipLevels - 1; TopMip++)
		{
			uint32_t dstWidth = std::max(uint32_t(textureDesc.Width >> (TopMip + 1)), uint32_t(1));
			uint32_t dstHeight = std::max(uint32_t(textureDesc.Height >> (TopMip + 1)), uint32_t(1));


			srcTextureSRVDesc.Format = Texture::GetUAVCompatableFormat(textureDesc.Format);
			srcTextureSRVDesc.Texture2D.MipLevels = 1;
			srcTextureSRVDesc.Texture2D.MostDetailedMip = TopMip;
			device.CreateShaderResourceView(texture, &srcTextureSRVDesc, currentCPUHandle);
			currentCPUHandle.Offset(1, descriptorSize);


			destTextureUAVDesc.Format = Texture::GetUAVCompatableFormat(textureDesc.Format);
			destTextureUAVDesc.Texture2D.MipSlice = TopMip + 1;
			device.CreateUnorderedAccessView(texture, nullptr, &destTextureUAVDesc, currentCPUHandle);
			currentCPUHandle.Offset(1, descriptorSize);

			//GenerateMipsCB mipData = {};			
			//mipData.TexelSize = Vector2{ (1.0f / dstWidth) ,(1.0f / dstHeight) };			
			//mipBuffer->CopyData(0, mipData);			
			//commandListDirect->SetComputeRootConstantBufferView(0, mipBuffer->Resource()->GetGPUVirtualAddress());

			Vector2 texelSize = Vector2{ (1.0f / dstWidth), (1.0f / dstHeight) };
			cmdList->SetComputeRoot32BitConstants(0, 2, &texelSize, 0);
			cmdList->SetComputeRootDescriptorTable(1, currentGPUHandle);
			currentGPUHandle.Offset(1, descriptorSize);
			
			cmdList->SetComputeRootDescriptorTable(2, currentGPUHandle);
			currentGPUHandle.Offset(1, descriptorSize);

			cmdList->Dispatch(std::max(dstWidth / 8, 1u), std::max(dstHeight / 8, 1u), 1);
			
			cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(texture));
		}
	}

	queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));
}


TextureUsage Texture::GetTextureType() const
{
	return usage;
}

UINT Texture::GetTextureIndex() const
{
	return textureIndex;
}

Texture::Texture(std::wstring name, TextureUsage use): GResource( name),
                                                                              usage(use)
{
	textureIndex = textureIndexGlobal++;
}

Texture::Texture(const D3D12_RESOURCE_DESC& resourceDesc, const std::wstring& name, TextureUsage textureUsage, const D3D12_CLEAR_VALUE* clearValue) : GResource(resourceDesc, name, clearValue),
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
	

	std::lock_guard<std::mutex> lock(shaderResourceViewsMutex);
	std::lock_guard<std::mutex> guard(unorderedAccessViewsMutex);

	// SRVs and UAVs will be created as needed.
	shaderResourceViews.clear();
	unorderedAccessViews.clear();
}

GMemory Texture::CreateShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) const
{
	auto& app = DXLib::D3DApp::GetApp();
	auto& device = app.GetDevice();

	auto srv = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	return (srv);
	
	device.CreateShaderResourceView(dxResource.Get(), srvDesc, srv.GetCPUHandle());

	
}

GMemory Texture::CreateUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc) const
{
	auto& app = DXLib::D3DApp::GetApp();
	auto& device = app.GetDevice();

	auto uav = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	device.CreateUnorderedAccessView(dxResource.Get(), nullptr, uavDesc, uav.GetCPUHandle());

	return uav;
}

GMemory Texture::CreateRenderTargetView(const D3D12_RENDER_TARGET_VIEW_DESC* rtvDesc) const
{
	auto& app = DXLib::D3DApp::GetApp();
	auto& device = app.GetDevice();

	auto srv = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	device.CreateRenderTargetView(dxResource.Get(), rtvDesc, srv.GetCPUHandle());

	return srv;
}

GMemory Texture::CreateDepthStencilView(const D3D12_DEPTH_STENCIL_VIEW_DESC* dsvDesc) const
{
	auto& app = DXLib::D3DApp::GetApp();
	auto& device = app.GetDevice();

	auto srv = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	device.CreateDepthStencilView(dxResource.Get(), dsvDesc, srv.GetCPUHandle());

	return srv;
}


Texture::~Texture()
{
}

DescriptorHandle Texture::GetShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) const
{
	std::size_t hash = 0;
	if (srvDesc)
	{
		hash = std::hash<D3D12_SHADER_RESOURCE_VIEW_DESC>{}(*srvDesc);
	}

	std::lock_guard<std::mutex> lock(shaderResourceViewsMutex);

	auto iter = shaderResourceViews.find(hash);
	if (iter == shaderResourceViews.end())
	{
		auto srv = CreateShaderResourceView(srvDesc);
		iter = shaderResourceViews.insert({hash, std::move(srv)}).first;
	}

	return DescriptorHandle{ iter->second.GetCPUHandle(), iter->second.GetGPUHandle() };
}

DescriptorHandle Texture::GetUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc) const
{
	std::size_t hash = 0;
	if (uavDesc)
	{
		hash = std::hash<D3D12_UNORDERED_ACCESS_VIEW_DESC>{}(*uavDesc);
	}

	std::lock_guard<std::mutex> guard(unorderedAccessViewsMutex);

	auto iter = unorderedAccessViews.find(hash);
	if (iter == unorderedAccessViews.end())
	{
		auto uav = CreateUnorderedAccessView(uavDesc);
		iter = unorderedAccessViews.insert({hash, std::move(uav)}).first;
	}

	return DescriptorHandle{ iter->second.GetCPUHandle(), iter->second.GetGPUHandle() };
}

DescriptorHandle Texture::GetRenderTargetView(const D3D12_RENDER_TARGET_VIEW_DESC* rtvDesc) const
{
	if (renderTargetView.IsNull())
	{
		auto& app = DXLib::D3DApp::GetApp();
		auto& device = app.GetDevice();

		CD3DX12_RESOURCE_DESC desc(dxResource->GetDesc());

		if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0)
		{
			renderTargetView = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			device.CreateRenderTargetView(dxResource.Get(), rtvDesc, renderTargetView.GetCPUHandle());
		}
		if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0)
		{
			depthStencilView = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
			device.CreateDepthStencilView(dxResource.Get(), nullptr, depthStencilView.GetCPUHandle());
		}
	}
	
	return DescriptorHandle{ renderTargetView.GetCPUHandle(), renderTargetView.GetGPUHandle() };
}

DescriptorHandle Texture::GetDepthStencilView(const D3D12_DEPTH_STENCIL_VIEW_DESC* dsvDesc) const
{
	if (depthStencilView.IsNull())
	{
		auto& app = DXLib::D3DApp::GetApp();
		auto& device = app.GetDevice();

		CD3DX12_RESOURCE_DESC desc(dxResource->GetDesc());

		if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0)
		{
			depthStencilView = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
			device.CreateDepthStencilView(dxResource.Get(), dsvDesc, depthStencilView.GetCPUHandle());
		}
	}
	
	return DescriptorHandle{ depthStencilView.GetCPUHandle(), depthStencilView.GetGPUHandle() };
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

static custom_unordered_map<std::wstring, std::shared_ptr<Texture>> cashedLoadTextures = DXAllocator::CreateUnorderedMap<std::wstring, std::shared_ptr<Texture>>();


std::shared_ptr<Texture> Texture::LoadTextureFromFile(std::wstring filepath,
                                     ID3D12GraphicsCommandList* commandList, TextureUsage usage)
{
	auto it = cashedLoadTextures.find(filepath);
	if(cashedLoadTextures.find(filepath) != cashedLoadTextures.end())
	{
	  return it->second;
	}
		
	std::filesystem::path filePath(filepath);
	if (!exists(filePath))
	{
		assert("File not found.");
	}
	

	DirectX::TexMetadata metadata;
	DirectX::ScratchImage scratchImage;


	UINT resFlags = D3D12_RESOURCE_FLAG_NONE;

	if (filePath.extension() == ".dds")
	{
		ThrowIfFailed(DirectX::LoadFromDDSFile(filepath.c_str(), DirectX::DDS_FLAGS_NONE , &metadata, scratchImage));
	}
	else if (filePath.extension() == ".hdr")
	{
		ThrowIfFailed(DirectX::LoadFromHDRFile(filepath.c_str(), &metadata, scratchImage));
	}
	else if (filePath.extension() == ".tga")
	{
		ThrowIfFailed(DirectX::LoadFromTGAFile(filepath.c_str(), &metadata, scratchImage));
	}
	else
	{
		ThrowIfFailed(
			DirectX::LoadFromWICFile(filepath.c_str(), DirectX::WIC_FLAGS_FORCE_RGB, &metadata, scratchImage));

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

	auto& device = DXLib::D3DApp::GetApp().GetDevice();


	ComPtr<ID3D12Resource> textureResource;
	
	ThrowIfFailed(device.CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&textureResource)));
		
	// Update the global state tracker.
	GResourceStateTracker::AddGlobalResourceState(textureResource.Get(), D3D12_RESOURCE_STATE_COMMON);
	
	std::vector<D3D12_SUBRESOURCE_DATA> subresources(scratchImage.GetImageCount());
	ThrowIfFailed(
		PrepareUpload(&device, scratchImage.GetImages(), scratchImage.GetImageCount(), scratchImage.GetMetadata(),
			subresources));

	if (textureResource)
	{
		
		
		commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(textureResource.Get(),
		                                                                      D3D12_RESOURCE_STATE_COMMON,
		                                                                      D3D12_RESOURCE_STATE_COPY_DEST));

		GResourceStateTracker::AddGlobalResourceState(textureResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
		
		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(textureResource.Get(),
		                                                            0, static_cast<unsigned int>(subresources.size()));
		
		auto upload = DXAllocator::UploadData(uploadBufferSize, nullptr, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
				


		UpdateSubresources(commandList,
			textureResource.Get(), &upload.d3d12Resource,
			upload.Offset, 0, static_cast<unsigned int>(subresources.size()),
		                   subresources.data());


		commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(textureResource.Get(),
		                                                                      D3D12_RESOURCE_STATE_COPY_DEST,
		                                                                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

		GResourceStateTracker::AddGlobalResourceState(textureResource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);


		auto tex = std::make_shared<Texture>(filepath, usage);
				
		tex->SetName(filepath);
		tex->SetD3D12Resource(textureResource);

		if (resFlags == D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
		{
			tex->HasMipMap = false;
		}

		cashedLoadTextures[filepath] = std::move(tex);

		return cashedLoadTextures[filepath];
	}
		
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
	return dxResource.Get();

}

std::wstring& Texture::GetName()
{
	return resourceName;
}

