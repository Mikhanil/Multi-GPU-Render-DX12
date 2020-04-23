#include "FrameResource.h"



FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount)
{
    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandListAllocator.GetAddressOf())));

    PassConstantBuffer = std::make_unique<ConstantBuffer<PassConstants>>(device, passCount);
   // ObjectConstantBuffer = std::make_unique<ConstantBuffer<ObjectConstants>>(device, objectCount);
	//MaterialConstantBuffer = std::make_unique<ConstantBuffer<MaterialConstants>>(device, materialCount);	
}

FrameResource::~FrameResource()
{

}