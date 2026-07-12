#pragma once
#include "DirectXBuffers.h"
#include "ShaderBuffersData.h"

using namespace PEPEngine;
using namespace Graphics;

struct ReflectionPassConstants : CommonPassConstants
{
    LightData DirectionalLight;
    UINT PointLightCount = 0;
    UINT SpotLightCount = 0;
    UINT LightPad0 = 0;
    UINT LightPad1 = 0;
    Vector4 SsrRaySettings = Vector4{80.0f, 0.45f, 0.18f, 0.85f};
    Vector4 SsrResolveSettings = Vector4{128.0f, 8.0f, 16.0f, 0.0f};
};

struct FrameResource
{
    FrameResource(std::shared_ptr<GDevice> primeDevice, std::shared_ptr<GDevice> secondDevice, UINT objectCount,
                  UINT materialCount, UINT pointLightCount, UINT spotLightCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    std::shared_ptr<ConstantUploadBuffer<ReflectionPassConstants>> MainPassConstantUploadBuffer = nullptr;
    std::shared_ptr<ConstantUploadBuffer<ReflectionPassConstants>> SecondMainPassConstantUploadBuffer = nullptr;
    std::shared_ptr<ConstantUploadBuffer<ReflectionPassConstants>> ShadowPassConstantUploadBuffer = nullptr;
    std::shared_ptr<ConstantUploadBuffer<SsaoConstants>> SsaoConstantUploadBuffer = nullptr;
    std::shared_ptr<StructuredUploadBuffer<MaterialConstants>> MaterialBuffer = nullptr;
    std::shared_ptr<StructuredUploadBuffer<LightData>> PointLightBuffer = nullptr;
    std::shared_ptr<StructuredUploadBuffer<LightData>> SpotLightBuffer = nullptr;
    UINT PointLightCapacity = 0;
    UINT SpotLightCapacity = 0;

    UINT64 FenceValue = 0;
};
