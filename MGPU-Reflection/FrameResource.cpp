#include "FrameResource.h"

#include "GCrossAdapterResource.h"
#include "GDevice.h"
#include "GDescriptor.h"

FrameResource::FrameResource(std::shared_ptr<GDevice> primeDevice, std::shared_ptr<GDevice> secondDevice,
                             UINT passCount, UINT materialCount)
{
    PrimePassConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<PassConstants>>(
        primeDevice, passCount + 1, primeDevice->GetName() + L" Forward Path Data"));

    SecondPassConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<PassConstants>>(
        secondDevice, passCount, secondDevice->GetName() + L" Forward Path Data"));

    SsaoConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<SsaoConstants>>(
        primeDevice, 1, primeDevice->GetName() + L" SSAO Path Data"));

    MaterialBuffers.push_back(
        std::make_shared<StructuredUploadBuffer<MaterialConstants>>(primeDevice, materialCount,
                                                                    primeDevice->GetName() + L" Materials Data"));

    MaterialBuffers.push_back(
        std::make_shared<StructuredUploadBuffer<MaterialConstants>>(secondDevice, materialCount,
                                                                    secondDevice->GetName() + L" Materials Data"));

    BackBufferRTVMemory = (primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
}

FrameResource::~FrameResource()
{
}
