#pragma once

#include "d3dUtil.h"
#include "MathHelper.h"
#include "DirectXBuffers.h"
#include "GameObject.h"



 
struct FrameResource
{
public:
    
    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandListAllocator;

    std::unique_ptr<ConstantBuffer<PassConstants>> PassConstantBuffer = nullptr;
    std::unique_ptr<ConstantBuffer<SsaoConstants>> SsaoConstantBuffer = nullptr;
    std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialBuffer = nullptr;

    UINT64 FenceValue = 0;
};