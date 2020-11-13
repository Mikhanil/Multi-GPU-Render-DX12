#include "FrameResource.h"
FrameResource::FrameResource(std::shared_ptr<GDevice> primeDevices, std::shared_ptr<GDevice> secondDevice, UINT passCount, UINT materialCount)
{
	PrimePassConstantBuffer = (std::make_shared<ConstantBuffer<PassConstants>>(primeDevices, passCount, primeDevices->GetName() + L"Prime Path Data Buffer"));
		
	SsaoConstantBuffer = (std::make_shared<ConstantBuffer<SsaoConstants>>(primeDevices, 1, primeDevices->GetName() + L" SSAO Path Data Buffer"));

	MaterialBuffer  = std::make_shared<UploadBuffer<MaterialConstants>>(primeDevices, materialCount, primeDevices->GetName() + L" Material Data Buffer ");

	BackBufferRTVMemory = (primeDevices->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
}

FrameResource::~FrameResource()
{

}