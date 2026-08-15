#pragma once

#include "GDescriptor.h"
#include "DirectXBuffers.h"
#include "GDevice.h"
#include "ShaderBuffersData.h"

using namespace PEPEngine;
using namespace Graphics;


struct ReflectionPassConstants : CommonPassConstants
{
    LightData DirectionalLight{};
    UINT PointLightCount = 0;
    UINT SpotLightCount = 0;
    UINT LightPad0 = 0;
    UINT LightPad1 = 0;
    float SsrMaxDistance = 100.0f;
    float SsrThickness = 0.5f;
    float SsrStride = 1.0f;
    float SsrIntensity = 1.0f;
    UINT SsrMaxSteps = 96;
    UINT SsrBinarySteps = 5;
    float SsrEdgeFadeScale = 24.0f;
    float SsrPad = 0.0f;
};

struct FrameResource
{
    FrameResource(std::shared_ptr<GDevice> primeDevice, std::shared_ptr<GDevice> secondDevice, UINT passCount,
                  UINT materialCount, UINT pointLightCount, UINT spotLightCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
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
