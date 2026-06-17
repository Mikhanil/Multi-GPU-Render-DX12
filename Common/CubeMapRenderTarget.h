#pragma once

#include <array>

#include "d3dUtil.h"
#include "GCommandList.h"
#include "GDescriptor.h"
#include "GTexture.h"

using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;

class CubeMapRenderTarget
{
public:
    static constexpr UINT FaceCount = 6;

    CubeMapRenderTarget(const std::shared_ptr<GDevice>& device, UINT size, DXGI_FORMAT format, DXGI_FORMAT depthFormat);

    void OnResize(UINT newSize);

    UINT GetSize() const;

    GTexture& GetCubeMap();
    GTexture& GetDepthMap();

    GDescriptor GetRTV(UINT faceIndex) const;
    GDescriptor* GetDSV();
    GDescriptor* GetSRV();

    const D3D12_VIEWPORT& GetViewport() const;
    const D3D12_RECT& GetScissorRect() const;

private:
    
    void BuildResources();
    void BuildDescriptors() const;

    std::shared_ptr<GDevice> device;

    UINT size = 0;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT depthFormat = DXGI_FORMAT_D32_FLOAT;

    GTexture cubeMap;
    GTexture depthMap;

    GDescriptor rtvMemory;
    GDescriptor dsvMemory;
    GDescriptor srvMemory;

    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissorRect{};
};
