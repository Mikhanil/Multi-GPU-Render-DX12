#include "Texture.h"
#include "DirectXTex.h"
#include "filesystem"
#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"
#include "ResourceUploadBatch.h"
#include <winrt/base.h>

UINT Texture::textureIndexGlobal = 0;

TextureUsage Texture::GetTextureType() const
{
	return usage;
}

UINT Texture::GetTextureIndex() const
{
	return textureIndex;
}

Texture::Texture(std::string name, std::wstring filename, TextureUsage use):
	Filename(std::move(filename)), Name(std::move(name)), usage(use)
{
	textureIndex = textureIndexGlobal++;
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
		ThrowIfFailed(DirectX::LoadFromWICFile(Filename.c_str(), DirectX::WIC_FLAGS_FORCE_RGB, &metadata, scratchImage));
		
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
	desc.MipLevels = resFlags == D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ? 0 : static_cast<UINT16>(metadata.mipLevels);
	desc.DepthOrArraySize = (metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE3D)
		? static_cast<UINT16>(metadata.depth)
		: static_cast<UINT16>(metadata.arraySize);
	desc.Format = metadata.format;
	desc.Flags = (D3D12_RESOURCE_FLAGS)resFlags;
	desc.SampleDesc.Count = 1;
	desc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);
	
	ThrowIfFailed(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&directxResource)));


	std::vector<D3D12_SUBRESOURCE_DATA> subresources(scratchImage.GetImageCount());
	ThrowIfFailed(PrepareUpload(device, scratchImage.GetImages(), scratchImage.GetImageCount(), scratchImage.GetMetadata(),
		subresources));

	if (directxResource)
	{		
		commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(directxResource.Get(),
			D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
		
		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(directxResource.Get(),
			0, static_cast<unsigned int>(subresources.size()));

		ComPtr<ID3D12Resource> textureUploadHeap;
		ThrowIfFailed(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(textureUploadHeap.GetAddressOf())));


		UpdateSubresources(commandList,
			directxResource.Get(), textureUploadHeap.Get(),
			0, 0, static_cast<unsigned int>(subresources.size()),
			subresources.data());
		
		track.push_back(textureUploadHeap);
		track.push_back(directxResource);
		
		
		commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(directxResource.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
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
	if (isLoaded)return directxResource.Get();

	return nullptr;
}

std::string& Texture::GetName()
{
	return Name;
}

std::wstring& Texture::GetFileName()
{
	return Filename;
}
