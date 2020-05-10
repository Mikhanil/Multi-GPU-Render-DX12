#pragma once
#include "d3dUtil.h"
#include "GeneratedMipsPSO.h"

enum class TextureUsage
{
	Albedo,
	Diffuse = Albedo,       // Treat Diffuse and Albedo textures the same.
	Heightmap,
	Depth = Heightmap,      // Treat height and depth textures the same.
	Normalmap,
	RenderTarget,           // Texture is used as a render target.
};


class Texture
{
	std::wstring Filename;
	std::string Name;
	Microsoft::WRL::ComPtr<ID3D12Resource> directxResource = nullptr;
	bool isLoaded = false;

	TextureUsage usage;

	std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> track;
	std::unique_ptr<GeneratedMipsPSO> m_GenerateMipsPSO;

    Microsoft::WRL::ComPtr < ID3D12DescriptorHeap> UAVdescriptorHeap;

public:

	Texture(std::string name, std::wstring filename, TextureUsage use = TextureUsage::Diffuse);
	static DXGI_FORMAT GetUAVCompatableFormat(DXGI_FORMAT format);

	void LoadTexture(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* commandList);
	
    void ClearTrack()
    {
        track.clear();    	
    }
	
	ID3D12Resource* GetResource() const;

	std::string& GetName();

	std::wstring& GetFileName();


    bool IsUAVCompatibleFormat(DXGI_FORMAT format)
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

    bool IsSRGBFormat(DXGI_FORMAT format)
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

    bool IsBGRFormat(DXGI_FORMAT format)
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

    bool IsDepthFormat(DXGI_FORMAT format)
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

    DXGI_FORMAT GetTypelessFormat(DXGI_FORMAT format)
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

	
};


