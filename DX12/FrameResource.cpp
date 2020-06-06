#include "FrameResource.h"



FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount)
{
    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandListAllocator.GetAddressOf())));

    PassConstantBuffer = std::make_unique<ConstantBuffer<PassConstants>>(device, passCount);
	
    MaterialBuffer = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount);
}

FrameResource::~FrameResource()
{

}