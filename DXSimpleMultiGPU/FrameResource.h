#pragma once

#include "d3dUtil.h"
#include "DirectXBuffers.h"
#include "ShaderBuffersData.h"
#include "GDeviceFactory.h"


struct FrameResource
{
public:

	FrameResource(std::shared_ptr<GDevice> primeDevice, UINT passCount, UINT objectCount, UINT materialCount);
	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator=(const FrameResource& rhs) = delete;
	~FrameResource();

	ComPtr<ID3D12Resource> crossAdapterResources[GraphicAdapterCount][globalCountFrameResources];
	
	std::unique_ptr<ConstantBuffer<PassConstants>> PassConstantBuffer = nullptr;
	std::unique_ptr<ConstantBuffer<SsaoConstants>> SsaoConstantBuffer = nullptr;
	std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialBuffer = nullptr;

	UINT64 FenceValue = 0;
};
