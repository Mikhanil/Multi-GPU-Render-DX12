#include "FrameResource.h"


FrameResource::FrameResource(std::shared_ptr<GDevice> primeDevice, UINT passCount, UINT objectCount, UINT materialCount)
{
	PassConstantUploadBuffer = std::make_unique<ConstantUploadBuffer<PassConstants>>(primeDevice,passCount, L"Forward Path Data");
	SsaoConstantUploadBuffer = std::make_unique<ConstantUploadBuffer<SsaoConstants>>(primeDevice,1, L"SSAO Path Data");
	MaterialBuffer = std::make_unique<StructuredUploadBuffer<MaterialConstants>>(primeDevice, materialCount, L"Materials Data");
}

FrameResource::~FrameResource()
{
}
