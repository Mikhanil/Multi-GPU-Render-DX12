#include "pch.h"
#include "BakedCubeMapRenderTarget.h"

BakedCubeMapRenderTarget::BakedCubeMapRenderTarget(const std::shared_ptr<GDevice>& device, 
                                        UINT size, DXGI_FORMAT format, DXGI_FORMAT depthFormat)
    : device(device), size(size), format(format), depthFormat(depthFormat)
{
    viewport = { 0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size), 0.0f, 1.0f };
    scissorRect = { 0, 0, static_cast<int>(size), static_cast<int>(size) };

    rtvMemory = this->device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FaceCount);
    dsvMemory = this->device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, FaceCount);
    srvMemory = this->device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, FaceCount);
    BuildResources();
    BuildDescriptors();
}

void BakedCubeMapRenderTarget::OnResize(UINT newSize)
{
    if (size == newSize) return;

    size = newSize;

    viewport = { 0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size), 0.0f, 1.0f };
    scissorRect = { 0, 0, static_cast<int>(size), static_cast<int>(size) };

    BuildResources();
    BuildDescriptors();
}

UINT BakedCubeMapRenderTarget::GetSize() const
{
    return size;
}

GTexture& BakedCubeMapRenderTarget::GetCubeMap()
{
    return cubeMap;
}

GTexture& BakedCubeMapRenderTarget::GetDepthMap()
{
    return depthMap;
}

GDescriptor BakedCubeMapRenderTarget::GetRTV(UINT faceIndex) const
{
    return rtvMemory.Offset(faceIndex);
}

GDescriptor BakedCubeMapRenderTarget::GetDSV(UINT faceIndex) const
{
    return dsvMemory.Offset(faceIndex);
}

GDescriptor* BakedCubeMapRenderTarget::GetSRV()
{
    return &srvMemory;
}

const D3D12_VIEWPORT& BakedCubeMapRenderTarget::GetViewport() const
{
    return viewport;
}

const D3D12_RECT& BakedCubeMapRenderTarget::GetScissorRect() const
{
    return scissorRect;
}


void BakedCubeMapRenderTarget::BuildResources()
{
    D3D12_RESOURCE_DESC cubeDesc{};
    cubeDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    cubeDesc.Alignment = 0;
    cubeDesc.Width = size;
    cubeDesc.Height = size;
    cubeDesc.DepthOrArraySize = FaceCount;
    cubeDesc.MipLevels = 1;
    cubeDesc.Format = format;
    cubeDesc.SampleDesc.Count = 1;
    cubeDesc.SampleDesc.Quality = 0;
    cubeDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    cubeDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    constexpr float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    const CD3DX12_CLEAR_VALUE clearColor(format, clear);

    cubeMap = GTexture(device, cubeDesc, L"BakedCubeMap", TextureUsage::RenderTarget, &clearColor);

    D3D12_RESOURCE_DESC depthDesc{};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Alignment = 0;
    depthDesc.Width = size;
    depthDesc.Height = size;
    depthDesc.DepthOrArraySize = FaceCount;
    depthDesc.MipLevels = 1;
    depthDesc.Format = depthFormat;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearDepth{};
    clearDepth.Format = depthFormat;
    clearDepth.DepthStencil.Depth = 1.0f;
    clearDepth.DepthStencil.Stencil = 0;

    depthMap = GTexture(device, depthDesc, L"BakedCubeMapDepth", TextureUsage::Depth, &clearDepth);
}

void BakedCubeMapRenderTarget::BuildDescriptors() const
{
    D3D12_SHADER_RESOURCE_VIEW_DESC cubeSrvDesc{};
    cubeSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    cubeSrvDesc.Format = format;
    cubeSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    cubeSrvDesc.TextureCube.MostDetailedMip = 0;
    cubeSrvDesc.TextureCube.MipLevels = 1;
    cubeSrvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
    cubeMap.CreateShaderResourceView(&cubeSrvDesc, &srvMemory);
    
    for (UINT i = 0; i < FaceCount; ++i)
    {
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = format;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rtvDesc.Texture2D.MipSlice = 0;
        rtvDesc.Texture2D.PlaneSlice = 0;
        
        // Render target to ith element.
        rtvDesc.Texture2DArray.FirstArraySlice = i;
        // Only view one element of the array.
        rtvDesc.Texture2DArray.ArraySize = 1;

        cubeMap.CreateRenderTargetView(&rtvDesc, &rtvMemory, i);
    }
    
    for (UINT i = 0; i < FaceCount; ++i)
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = depthFormat;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
        dsvDesc.Texture2D.MipSlice = 0;
        
        dsvDesc.Texture2DArray.FirstArraySlice = i;
        dsvDesc.Texture2DArray.ArraySize = 1;
        
        depthMap.CreateDepthStencilView(&dsvDesc, &dsvMemory, i);
    }
    
}
