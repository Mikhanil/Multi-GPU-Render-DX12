#pragma once

#include "d3dUtil.h"

const int globalCountFrameResources = 3;

 
class FrameResource
{
public:
    
    FrameResource(ID3D12Device* device);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();
	
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandListAllocator;   	
    UINT64 FenceValue = 0;
};