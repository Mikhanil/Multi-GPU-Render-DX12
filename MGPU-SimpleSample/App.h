#pragma once
#include "d3dApp.h"
#include "FrameResource.h"
#include "GCrossAdapterResource.h"
#include "GDescriptor.h"
#include "GTexture.h"


class App :
    public Common::D3DApp
{
public:
    App(HINSTANCE hInstance);

    bool Initialize() override;

protected:
    void Update(const GameTimer& gt) override;
    void Draw(const GameTimer& gt) override;

    void OnResize() override;

    bool InitMainWindow() override;;

private:
    UINT backBufferIndex = 0;
    DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D32_FLOAT;

    D3D12_VIEWPORT primeViewport{};
    D3D12_RECT fullrect{};
    D3D12_RECT primeRect{};
    D3D12_RECT secondRect{};
    D3D12_BOX copyRegionBox;

    std::shared_ptr<GDevice> primeDevice;
    std::shared_ptr<GDevice> secondDevice;

    std::vector<std::unique_ptr<GCrossAdapterResource>> crossAdapterBackBuffers = std::vector<
        std::unique_ptr<GCrossAdapterResource>>();

    std::vector<GResource> primeDeviceBackBuffers = std::vector<GResource>();

    GDescriptor primeRTV;
    GDescriptor sharedRTV;

    ComPtr<ID3D12Fence> primeFence;
    ComPtr<ID3D12Fence> sharedFence;
    UINT64 sharedFenceValue = 0;


    std::vector<std::unique_ptr<FrameResource>> frameResources = std::vector<std::unique_ptr<
        FrameResource>>();

    FrameResource* currentFrameResource = nullptr;
    int currentFrameResourceIndex = 0;
};
