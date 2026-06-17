#include "FrameResource.h"
#include "SharedHBAO.h"

FrameResource::FrameResource(std::shared_ptr<GDevice> primeDevices, std::shared_ptr<GDevice> secondDevice,
                             UINT passCount, UINT materialCount)
{
    PrimePassConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<PassConstants>>(
        primeDevices, passCount, primeDevices->GetName() + L"Prime Path Data Buffer"));

    PrimeSsaoConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<SsaoConstants>>(
        primeDevices, 1, primeDevices->GetName() + L" SSAO Path Data Buffer"));

    PrimeHBAOConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<HBAOConstants>>(
        primeDevices, 1, primeDevices->GetName() + L"HBAO Path Data Buffer"));

    MaterialBuffer = std::make_shared<StructuredUploadBuffer<MaterialConstants>>(
        primeDevices, materialCount, primeDevices->GetName() + L" Material Data Buffer ");

    /*
    * Atmosphere Buffers
    */

    PrimeAtmosphereUploadBuffer = std::make_shared<ConstantUploadBuffer<AtmosphereConstants>>(
        primeDevices, 1, primeDevices->GetName() + L" SkyAtmosphere CB");

    SecondAtmosphereUploadBuffer = std::make_shared<ConstantUploadBuffer<AtmosphereConstants>>(
        secondDevice, 1, secondDevice->GetName() + L" SkyAtmosphere CB");

    PrimeAtmosphereCommonUploadBuffer = std::make_shared<ConstantUploadBuffer<AtmosphereCommonConstants>>(
        primeDevices, 1, primeDevices->GetName() + L" SkyAtmosphereCommon CB");

	SecondAtmosphereCommonUploadBuffer = std::make_shared<ConstantUploadBuffer<AtmosphereCommonConstants>>(
        secondDevice, 1, secondDevice->GetName() + L" SkyAtmosphereCommon CB");

    BackBufferRTVMemory = (primeDevices->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
}

FrameResource::~FrameResource()
{
}
