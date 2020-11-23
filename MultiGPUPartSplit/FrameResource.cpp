#include "FrameResource.h"

#include "GCrossAdapterResource.h"
#include "GDevice.h"
#include "GDescriptor.h"

FrameResource::FrameResource(std::shared_ptr<GDevice> primeDevices, std::shared_ptr<GDevice> secondDevice, UINT passCount, UINT materialCount)
{
	PrimePassConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<PassConstants>>(primeDevices, passCount + 1, primeDevices->GetName() + L" Forward Path Data"));

	ShadowPassConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<PassConstants>>(secondDevice, passCount, secondDevice->GetName() + L" Forward Path Data"));
		
	SsaoConstantUploadBuffer = (std::make_shared<ConstantUploadBuffer<SsaoConstants>>(primeDevices, 1, primeDevices->GetName() + L" SSAO Path Data"));

	MaterialBuffers.push_back(std::make_shared<StructuredUploadBuffer<MaterialConstants>>(primeDevices, materialCount, primeDevices->GetName() + L" Materials Data"));

	MaterialBuffers.push_back(std::make_shared<StructuredUploadBuffer<MaterialConstants>>(secondDevice, materialCount, secondDevice->GetName() + L" Materials Data"));

	BackBufferRTVMemory = (primeDevices->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));	
	
}

FrameResource::~FrameResource()
{
	
}
