#include "Texture.h"
#include "DirectXTex.h"
#include "filesystem"
#include "DDSTextureLoader.h"
#include "ResourceUploadBatch.h"
#include <winrt/base.h>

using namespace std::filesystem;
using namespace DirectX;

Texture::Texture(std::string name, std::wstring filename, TextureUsage use):
	Filename(std::move(filename)), Name(std::move(name)), usage(use)
{
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

	winrt::init_apartment();
	
	//DirectX::ResourceUploadBatch upload(device);
	//upload.Begin();

	//ThrowIfFailed(DirectX::CreateDDSTextureFromFile(device,
	//	upload, Filename.c_str(), directxResource.GetAddressOf()));


	//// Upload the resources to the GPU.
	//upload.End(queue).wait();
	
	path filePath(Filename);
	if (!exists(filePath))
	{
		assert("File not found.");
	}

	TexMetadata metadata;
	ScratchImage scratchImage;

	UINT resFlags = D3D12_RESOURCE_FLAG_NONE;
	
	if (filePath.extension() == ".dds")
	{
		ThrowIfFailed(LoadFromDDSFile(Filename.c_str(), DDS_FLAGS_FORCE_RGB , &metadata, scratchImage));
	}
	else if (filePath.extension() == ".hdr")
	{
		ThrowIfFailed(LoadFromHDRFile(Filename.c_str(), &metadata, scratchImage));
	}
	else if (filePath.extension() == ".tga")
	{
		ThrowIfFailed(LoadFromTGAFile(Filename.c_str(), &metadata, scratchImage));
	}
	else
	{
		ThrowIfFailed(LoadFromWICFile(Filename.c_str(), WIC_FLAGS_FORCE_RGB, &metadata, scratchImage));
		//Если это не DDS или "специфичная" текстура, то для нее нужно будет генерировать мипмапы
		//по этому даем возможность сделать ее UAV
		resFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}

	/*Пока выключил гамма корекцию.*/
	//if (usage == TextureUsage::Albedo)
	//{	
	//	metadata.format = GetTypelessFormat(metadata.format); // MakeSRGB(metadata.format);
	//}

	D3D12_RESOURCE_DESC desc = {};
	desc.Width = static_cast<UINT>(metadata.width);
	desc.Height = static_cast<UINT>(metadata.height);
	/*
	 * DDS текстуры нельзя использовать как UAV для генерации мипмап карт.
	 */	
	desc.MipLevels = resFlags == D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ? 0 : static_cast<UINT16>(metadata.mipLevels);
	desc.DepthOrArraySize = (metadata.dimension == TEX_DIMENSION_TEXTURE3D)
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
	ThrowIfFailed(PrepareUpload(device, scratchImage.GetImages(), scratchImage.GetImageCount(), metadata,
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
