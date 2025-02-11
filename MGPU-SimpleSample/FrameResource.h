#pragma once

#include "DirectXBuffers.h"
#include "ShaderBuffersData.h"
#include "GDeviceFactory.h"

using namespace PEPEngine;
using namespace Graphics;

struct FrameResource
{
    FrameResource(std::shared_ptr<GDevice> primeDevice, UINT passCount, UINT objectCount, UINT materialCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    ComPtr<ID3D12Resource> crossAdapterResources[GraphicAdapterCount][globalCountFrameResources];

    std::unique_ptr<ConstantUploadBuffer<PassConstants>> PassConstantUploadBuffer = nullptr;
    std::unique_ptr<ConstantUploadBuffer<SsaoConstants>> SsaoConstantUploadBuffer = nullptr;
    std::unique_ptr<StructuredUploadBuffer<MaterialConstants>> MaterialBuffer = nullptr;

    UINT64 FenceValue = 0;
};
