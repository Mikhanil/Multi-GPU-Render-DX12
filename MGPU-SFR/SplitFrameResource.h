#pragma once

#include "ShaderBuffersData.h"
#include "GTexture.h"

using namespace PEPEngine;
using namespace Graphics;

class GCrossAdapterResource;

struct SplitFrameResource
{
    SplitFrameResource(std::shared_ptr<GDevice>* devices, UINT deviceCount, UINT passCount, UINT materialCount);
    SplitFrameResource(const SplitFrameResource& rhs) = delete;
    SplitFrameResource& operator=(const SplitFrameResource& rhs) = delete;
    ~SplitFrameResource();


    std::shared_ptr<GCrossAdapterResource> CrossAdapterBackBuffer = nullptr;

    GTexture PrimeDeviceBackBuffer;

    std::vector<GDescriptor> RenderTargetViewMemory = std::vector<GDescriptor>();

    std::vector<std::shared_ptr<ConstantUploadBuffer<PassConstants>>> PassConstantUploadBuffers =
        std::vector<std::shared_ptr<ConstantUploadBuffer<PassConstants>>>();

    std::vector<std::shared_ptr<ConstantUploadBuffer<SsaoConstants>>> SsaoConstantUploadBuffers =
        std::vector<std::shared_ptr<ConstantUploadBuffer<SsaoConstants>>>();

    std::vector<std::shared_ptr<StructuredUploadBuffer<MaterialConstants>>> MaterialBuffers =
        std::vector<std::shared_ptr<StructuredUploadBuffer<MaterialConstants>>>();


    UINT64 FenceValue = 0;
};
