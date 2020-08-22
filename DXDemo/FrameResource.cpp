#include "FrameResource.h"


FrameResource::FrameResource(UINT passCount, UINT objectCount, UINT materialCount)
{
	PassConstantBuffer = std::make_unique<ConstantBuffer<PassConstants>>(passCount, L"Forward Path Data");
	SsaoConstantBuffer = std::make_unique<ConstantBuffer<SsaoConstants>>(1, L"SSAO Path Data");
	MaterialBuffer = std::make_unique<UploadBuffer<MaterialConstants>>(materialCount, L"Materials Data");
}

FrameResource::~FrameResource()
{
}
