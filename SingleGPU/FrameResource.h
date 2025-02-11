#pragma once
#include "DirectXBuffers.h"
#include "ShaderBuffersData.h"

using namespace PEPEngine;
using namespace Graphics;


struct FrameResource
{
    FrameResource(std::shared_ptr<GDevice> primeDevice, UINT passCount, UINT objectCount, UINT materialCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    std::shared_ptr<ConstantUploadBuffer<PassConstants>> PassConstantUploadBuffer = nullptr;
    std::shared_ptr<ConstantUploadBuffer<SsaoConstants>> SsaoConstantUploadBuffer = nullptr;
    std::shared_ptr<StructuredUploadBuffer<MaterialConstants>> MaterialBuffer = nullptr;

    UINT64 FenceValue = 0;
};
