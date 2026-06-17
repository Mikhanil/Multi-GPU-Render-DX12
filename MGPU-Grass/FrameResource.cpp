#include "FrameResource.h"

FrameResource::FrameResource(std::shared_ptr<GDevice> primeDevices, std::shared_ptr<GDevice> secondDevice,
                             UINT passCount, UINT materialCount)
{
    PrimePassConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<PassConstants>>(
        primeDevices, passCount, primeDevices->GetName() + L"Prime Path Data Buffer"));

    SsaoConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<SsaoConstants>>(
        primeDevices, 1, primeDevices->GetName() + L" SSAO Path Data Buffer"));

    MaterialBuffer = std::make_shared<StructuredUploadBuffer<MaterialConstants>>(
        primeDevices, materialCount, primeDevices->GetName() + L" Material Data Buffer ");

    BackBufferRTVMemory = (primeDevices->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
}

FrameResource::~FrameResource()
{
}
