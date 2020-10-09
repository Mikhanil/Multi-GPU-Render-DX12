#pragma once
#include "DirectXBuffers.h"
#include "ShaderBuffersData.h"


struct FrameResource
{
public:

	FrameResource(std::shared_ptr<GDevice> primeDevice, UINT passCount, UINT objectCount, UINT materialCount);
	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator=(const FrameResource& rhs) = delete;
	~FrameResource();
	
	std::shared_ptr<ConstantBuffer<PassConstants>> PassConstantBuffer = nullptr;
	std::shared_ptr<ConstantBuffer<SsaoConstants>> SsaoConstantBuffer = nullptr;
	std::shared_ptr<UploadBuffer<MaterialConstants>> MaterialBuffer = nullptr;

	UINT64 FenceValue = 0;
};
