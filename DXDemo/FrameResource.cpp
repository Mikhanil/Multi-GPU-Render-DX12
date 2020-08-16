#include "FrameResource.h"


FrameResource::FrameResource(UINT passCount, UINT objectCount, UINT materialCount)
{
	PassConstantBuffer = std::make_unique<ConstantBuffer<PassConstants>>(passCount);
	SsaoConstantBuffer = std::make_unique<ConstantBuffer<SsaoConstants>>(1);
	MaterialBuffer = std::make_unique<UploadBuffer<MaterialConstants>>(materialCount);
}

FrameResource::~FrameResource()
{
}
