#include "FrameResource.h"


FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount)
{
	PassConstantBuffer = std::make_unique<ConstantBuffer<PassConstants>>(device, passCount);
	SsaoConstantBuffer = std::make_unique<ConstantBuffer<SsaoConstants>>(device, 1);
	MaterialBuffer = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount);
}

FrameResource::~FrameResource()
{
}
