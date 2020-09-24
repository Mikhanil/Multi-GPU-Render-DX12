#include "FrameResource.h"


FrameResource::FrameResource(std::shared_ptr<GDevice> primeDevice, UINT passCount, UINT objectCount, UINT materialCount)
{
	PassConstantBuffer = std::make_shared<ConstantBuffer<PassConstants>>(primeDevice,passCount, L"Forward Path Data");
	SsaoConstantBuffer = std::make_shared<ConstantBuffer<SsaoConstants>>(primeDevice,1, L"SSAO Path Data");
	MaterialBuffer = std::make_shared<UploadBuffer<MaterialConstants>>(primeDevice, materialCount, L"Materials Data");
}

FrameResource::~FrameResource()
{
}
