#include "pch.h"
#include "ShadowMap.h"
#include "d3dApp.h"
#include "MemoryAllocator.h"
#include "GResourceStateTracker.h"

ShadowMap::ShadowMap(std::shared_ptr<GDevice> device, UINT width, UINT height) : device(device), width(width),
    height(height)
{
    viewport = {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    scissorRect = {0, 0, static_cast<int>(width), static_cast<int>(height)};

    srvMemory = this->device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    dsvMemory = this->device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

    BuildResource();
    BuildViews();
}

ShadowMap::ShadowMap(std::shared_ptr<GDevice> device, UINT width, UINT height, GTexture& texture): device(device),
    width(width), height(height)
{
    viewport = {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    scissorRect = {0, 0, static_cast<int>(width), static_cast<int>(height)};

    srvMemory = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    dsvMemory = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

    shadowMap = texture;

    BuildViews();
}

ShadowMap::~ShadowMap() = default;

GDescriptor* ShadowMap::GetSrv()
{
    return &srvMemory;
}

GDescriptor* ShadowMap::GetDsv()
{
    return &dsvMemory;
}

UINT ShadowMap::Width() const
{
    return width;
}

UINT ShadowMap::Height() const
{
    return height;
}

GTexture& ShadowMap::GetTexture()
{
    return shadowMap;
}


void ShadowMap::OnResize(const UINT newWidth, const UINT newHeight)
{
    if ((width != newWidth) || (height != newHeight))
    {
        width = newWidth;
        height = newHeight;

        BuildResource();
        BuildViews();
    }
}

void ShadowMap::PopulatePreRenderCommands(const std::shared_ptr<GCommandList>& cmdList) const
{
    cmdList->SetViewports(&viewport, 1);
    cmdList->SetScissorRects(&scissorRect, 1);

    cmdList->TransitionBarrier(shadowMap, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    cmdList->FlushResourceBarriers();
    cmdList->SetRenderTargets(0, nullptr, 0, &dsvMemory, 0);
    cmdList->ClearDepthStencil(&dsvMemory, 0,
                               D3D12_CLEAR_FLAG_DEPTH);
}

void ShadowMap::BuildViews() const
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    srvDesc.Texture2D.PlaneSlice = 0;
    shadowMap.CreateShaderResourceView(&srvDesc, &srvMemory);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.Texture2D.MipSlice = 0;
    shadowMap.CreateDepthStencilView(&dsvDesc, &dsvMemory);
}

void ShadowMap::BuildResource()
{
    if (!shadowMap.IsValid())
    {
        D3D12_RESOURCE_DESC texDesc;
        ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Alignment = 0;
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE optClear;
        optClear.Format = DXGI_FORMAT_D32_FLOAT;
        optClear.DepthStencil.Depth = 1.0f;

        shadowMap = GTexture(device, texDesc, std::wstring(L"Shadow Map"), TextureUsage::Depth, &optClear);
    }
    else
    {
        GTexture::Resize(shadowMap, width, height, 1);
    }
}
