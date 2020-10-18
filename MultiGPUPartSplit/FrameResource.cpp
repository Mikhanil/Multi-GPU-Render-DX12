#include "FrameResource.h"

#include "GCrossAdapterResource.h"
#include "GDevice.h"
#include "GDescriptor.h"

FrameResource::FrameResource(std::shared_ptr<GDevice> primeDevices, std::shared_ptr<GDevice> secondDevice, UINT passCount, UINT materialCount)
{
	PrimePassConstantBuffer = (std::make_shared<ConstantBuffer<PassConstants>>(primeDevices, passCount + 1, primeDevices->GetName() + L" Forward Path Data"));

	ShadowPassConstantBuffer = (std::make_shared<ConstantBuffer<PassConstants>>(secondDevice, passCount, secondDevice->GetName() + L" Forward Path Data"));
		
	SsaoConstantBuffer = (std::make_shared<ConstantBuffer<SsaoConstants>>(primeDevices, 1, primeDevices->GetName() + L" SSAO Path Data"));

	MaterialBuffers.push_back(std::make_shared<UploadBuffer<MaterialConstants>>(primeDevices, materialCount, primeDevices->GetName() + L" Materials Data"));

	MaterialBuffers.push_back(std::make_shared<UploadBuffer<MaterialConstants>>(secondDevice, materialCount, secondDevice->GetName() + L" Materials Data"));

	BackBufferRTVMemory = (primeDevices->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));	
	
}

FrameResource::~FrameResource()
{
	
}
