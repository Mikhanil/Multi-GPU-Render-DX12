#include "FrameResource.h"


FrameResource::FrameResource(std::shared_ptr<GDevice> device, UINT passCount, UINT objectCount, UINT materialCount)
{
	PassConstantBuffer = std::make_unique<ConstantBuffer<PassConstants>>(device,passCount, L"Forward Path Data");
	SsaoConstantBuffer = std::make_unique<ConstantBuffer<SsaoConstants>>(device,1, L"SSAO Path Data");
	MaterialBuffer = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, L"Materials Data");
}

FrameResource::~FrameResource()
{
}
