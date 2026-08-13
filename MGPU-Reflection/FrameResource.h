#pragma once

#include "GDescriptor.h"
#include "ShaderBuffersData.h"
#include "GTexture.h"

using namespace PEPEngine;
using namespace Graphics;


struct ReflectionPassConstants : CommonPassConstants
{
    LightData DirectionalLight{};
    UINT PointLightCount = 0;
    UINT SpotLightCount = 0;
    UINT LightPad0 = 0;
    UINT LightPad1 = 0;
};

struct FrameResource
{
    FrameResource(std::shared_ptr<GDevice> primeDevice, std::shared_ptr<GDevice> secondDevice, UINT passCount,
                  UINT materialCount, UINT pointLightCount, UINT spotLightCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();


    GDescriptor BackBufferRTVMemory;

    std::shared_ptr<ConstantUploadBuffer<ReflectionPassConstants>> PrimePassConstantUploadBuffer;
    std::shared_ptr<ConstantUploadBuffer<ReflectionPassConstants>> SecondPassConstantUploadBuffer;
    std::shared_ptr<ConstantUploadBuffer<SsaoConstants>> SsaoConstantUploadBuffer;

    std::vector<std::shared_ptr<StructuredUploadBuffer<MaterialConstants>>> MaterialBuffers =
        std::vector<std::shared_ptr<StructuredUploadBuffer<MaterialConstants>>>();
    std::vector<std::shared_ptr<StructuredUploadBuffer<LightData>>> PointLightBuffers =
        std::vector<std::shared_ptr<StructuredUploadBuffer<LightData>>>();
    std::vector<std::shared_ptr<StructuredUploadBuffer<LightData>>> SpotLightBuffers =
        std::vector<std::shared_ptr<StructuredUploadBuffer<LightData>>>();
    UINT PointLightCapacity = 0;
    UINT SpotLightCapacity = 0;


    UINT64 PrimeRenderFenceValue = 0;
    UINT64 PrimeCopyFenceValue = 0;
    UINT64 SecondRenderFenceValue = 0;
};
