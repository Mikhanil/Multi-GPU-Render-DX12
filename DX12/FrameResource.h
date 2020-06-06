#pragma once

#include "d3dUtil.h"
#include "MathHelper.h"
#include "DirectXBuffers.h"
#include "GameObject.h"

struct Vertex
{
    Vertex(){}
    Vertex(float x, float y, float z, float nx, float ny, float nz, float u, float v) :
        position(x, y, z),
        normal(nx, ny, nz),
        texCord(u, v) {}
	
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 texCord;
};

 
struct FrameResource
{
public:
    
    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    // We cannot reset the allocator until the GPU is done processing the commands.
    // So each frame needs their own allocator.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandListAllocator;

    // We cannot update a cbuffer until the GPU is done processing the commands
    // that reference it.  So each frame needs their own cbuffers.
    std::unique_ptr<ConstantBuffer<PassConstants>> PassConstantBuffer = nullptr;

    std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialBuffer = nullptr;

    // Fence value to mark commands up to this fence point.  This lets us
    // check if these frame resources are still in use by the GPU.
    UINT64 FenceValue = 0;
};