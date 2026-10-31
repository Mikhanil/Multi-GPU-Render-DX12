#pragma once
#include "GDescriptor.h"
#include "GTexture.h"


using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;

class SSAA
{
    GTexture renderTarget;
    GTexture depthMap;

    GDescriptor srvMemory;
    GDescriptor rtvMemory;
    GDescriptor dsvMemory;

    UINT ResolutionMultiplier = 1;
    const DXGI_FORMAT rtvFormat = GetSRGBFormat(DXGI_FORMAT_R8G8B8A8_UNORM);
    DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D32_FLOAT;

    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissorRect{};

    std::shared_ptr<GDevice> device;

public:
    D3D12_VIEWPORT GetViewPort() const;

    D3D12_RECT GetRect() const;

    void SetMultiplier(UINT multi, UINT newWidth, UINT newHeight);

    GTexture& GetRenderTarget();

    GTexture& GetDepthMap();

    GDescriptor* GetRTV();

    GDescriptor* GetSRV();

    GDescriptor* GetDSV();

    void OnResize(UINT newWidth, UINT newHeight);

    SSAA(const std::shared_ptr<GDevice>& device, UINT multiplier, UINT width, UINT height,
        DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D32_FLOAT);
};
