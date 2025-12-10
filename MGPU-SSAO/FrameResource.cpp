#include "FrameResource.h"
#include "SharedHBAO.h"

FrameResource::FrameResource(std::shared_ptr<GDevice> primeDevices, std::shared_ptr<GDevice> secondDevice,
                             UINT passCount, UINT materialCount)
{
    PrimePassConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<PassConstants>>(
        primeDevices, passCount, primeDevices->GetName() + L"Prime Path Data Buffer"));

    PrimeSsaoConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<SsaoConstants>>(
        primeDevices, 1, primeDevices->GetName() + L" SSAO Path Data Buffer"));

    SecondSsaoConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<SsaoConstants>>(
        secondDevice, 1, secondDevice->GetName() + L" SSAO Path Data Buffer"));

    PrimeHBAOConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<HBAOConstants>>(
        primeDevices, 1, primeDevices->GetName() + L"HBAO Path Data Buffer"));

    SecondHBAOConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<HBAOConstants>>(
        secondDevice, 1, secondDevice->GetName() + L" HBAO Path Data Buffer"));
    
    MaterialBuffer = std::make_shared<StructuredUploadBuffer<MaterialConstants>>(
        primeDevices, materialCount, primeDevices->GetName() + L" Material Data Buffer ");

    BackBufferRTVMemory = (primeDevices->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
}

FrameResource::~FrameResource()
{
}
