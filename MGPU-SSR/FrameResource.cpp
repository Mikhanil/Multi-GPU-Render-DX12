#include "FrameResource.h"


FrameResource::FrameResource(std::shared_ptr<GDevice> primeDevice, std::shared_ptr<GDevice> secondDevice,
                             UINT objectCount, UINT materialCount, UINT pointLightCount, UINT spotLightCount)
{
    PointLightCapacity = pointLightCount > 0 ? pointLightCount : 1;
    SpotLightCapacity = spotLightCount > 0 ? spotLightCount : 1;

    MainPassConstantUploadBuffer = std::make_shared<ConstantUploadBuffer<ReflectionPassConstants>>(
        primeDevice, 1, L"Main Pass Data");
    ReflectionProbePassConstantUploadBuffer = std::make_shared<ConstantUploadBuffer<ReflectionPassConstants>>(
        primeDevice, kReflectionProbeCount * 6, L"Reflection Probe Pass Data");
    ReflectionProbeConstantUploadBuffer = std::make_shared<ConstantUploadBuffer<ReflectionProbeConstants>>(
        primeDevice, 1, L"Reflection Probe Data");
    if (secondDevice != nullptr)
    {
        SecondMainPassConstantUploadBuffer = std::make_shared<ConstantUploadBuffer<ReflectionPassConstants>>(
            secondDevice, 1, L"Second GPU SSR Main Pass Data");
        SecondReflectionProbePassConstantUploadBuffer =
            std::make_shared<ConstantUploadBuffer<ReflectionPassConstants>>(
                secondDevice, kReflectionProbeCount * 6, L"Second GPU Reflection Probe Pass Data");
        SecondShadowPassConstantUploadBuffer = std::make_shared<ConstantUploadBuffer<ReflectionPassConstants>>(
            secondDevice, 1, L"Second GPU Shadow Pass Data");
        SecondMaterialBuffer = std::make_shared<StructuredUploadBuffer<MaterialConstants>>(
            secondDevice, materialCount, L"Second GPU Materials Data");
        SecondPointLightBuffer = std::make_shared<StructuredUploadBuffer<LightData>>(
            secondDevice, PointLightCapacity, L"Second GPU Point Lights Data");
        SecondSpotLightBuffer = std::make_shared<StructuredUploadBuffer<LightData>>(
            secondDevice, SpotLightCapacity, L"Second GPU Spot Lights Data");
    }
    ShadowPassConstantUploadBuffer = std::make_shared<ConstantUploadBuffer<ReflectionPassConstants>>(
        primeDevice, 1, L"Shadow Pass Data");
    SsaoConstantUploadBuffer = std::make_shared<ConstantUploadBuffer<SsaoConstants>>(primeDevice, 1, L"SSAO Path Data");
    MaterialBuffer = std::make_shared<StructuredUploadBuffer<MaterialConstants>>(
        primeDevice, materialCount, L"Materials Data");
    PointLightBuffer = std::make_shared<StructuredUploadBuffer<LightData>>(
        primeDevice, PointLightCapacity, L"Point Lights Data");
    SpotLightBuffer = std::make_shared<StructuredUploadBuffer<LightData>>(
        primeDevice, SpotLightCapacity, L"Spot Lights Data");
}

FrameResource::~FrameResource()
{
}
