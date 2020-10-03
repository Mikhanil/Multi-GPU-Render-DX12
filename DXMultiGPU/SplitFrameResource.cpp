#include "SplitFrameResource.h"

#include "GCrossAdapterResource.h"
#include "GDevice.h"

SplitFrameResource::SplitFrameResource(std::shared_ptr<GDevice>* devices, UINT deviceCount, UINT passCount, UINT materialCount)
{
	for (auto i =0; i < deviceCount; ++i)
	{
		PassConstantBuffers.push_back(std::make_shared<ConstantBuffer<PassConstants>>(devices[i], passCount, devices[i]->GetName() + L" Forward Path Data"));
		SsaoConstantBuffers.push_back(std::make_shared<ConstantBuffer<SsaoConstants>>(devices[i], 1, devices[i]->GetName() + L" SSAO Path Data"));
		MaterialBuffers.push_back(std::make_shared<UploadBuffer<MaterialConstants>>(devices[i], materialCount, devices[i]->GetName() + L" Materials Data"));
		RenderTargetViewMemory.push_back(devices[i]->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
	}	
}

SplitFrameResource::~SplitFrameResource()
{
	PassConstantBuffers.clear();
	SsaoConstantBuffers.clear();
	MaterialBuffers.clear();
	CrossAdapterBackBuffer.reset();
	PrimeDeviceBackBuffer.Reset();
	RenderTargetViewMemory.clear();
}
