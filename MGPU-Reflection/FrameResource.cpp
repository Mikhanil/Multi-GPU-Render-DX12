#include "FrameResource.h"

#include "GDevice.h"
#include "GDescriptor.h"

FrameResource::FrameResource(std::shared_ptr<GDevice> primeDevice, std::shared_ptr<GDevice> secondDevice,
                             UINT passCount, UINT materialCount, UINT pointLightCount, UINT spotLightCount)
{
    PointLightCapacity = pointLightCount > 0 ? pointLightCount : 1;
    SpotLightCapacity = spotLightCount > 0 ? spotLightCount : 1;

    PrimePassConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<ReflectionPassConstants>>(
        primeDevice, passCount + 1, primeDevice->GetName() + L" Forward Path Data"));

    SecondPassConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<ReflectionPassConstants>>(
        secondDevice, passCount, secondDevice->GetName() + L" Forward Path Data"));

    SsaoConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<SsaoConstants>>(
        primeDevice, 1, primeDevice->GetName() + L" SSAO Path Data"));

    MaterialBuffers.push_back(
        std::make_shared<StructuredUploadBuffer<MaterialConstants>>(primeDevice, materialCount,
                                                                    primeDevice->GetName() + L" Materials Data"));

    MaterialBuffers.push_back(
        std::make_shared<StructuredUploadBuffer<MaterialConstants>>(secondDevice, materialCount,
                                                                    secondDevice->GetName() + L" Materials Data"));
    PointLightBuffers.push_back(
        std::make_shared<StructuredUploadBuffer<LightData>>(primeDevice, PointLightCapacity,
                                                            primeDevice->GetName() + L" Point Lights Data"));
    PointLightBuffers.push_back(
        std::make_shared<StructuredUploadBuffer<LightData>>(secondDevice, PointLightCapacity,
                                                            secondDevice->GetName() + L" Point Lights Data"));
    SpotLightBuffers.push_back(
        std::make_shared<StructuredUploadBuffer<LightData>>(primeDevice, SpotLightCapacity,
                                                            primeDevice->GetName() + L" Spot Lights Data"));
    SpotLightBuffers.push_back(
        std::make_shared<StructuredUploadBuffer<LightData>>(secondDevice, SpotLightCapacity,
                                                            secondDevice->GetName() + L" Spot Lights Data"));

    BackBufferRTVMemory = (primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
}
