#pragma once

#include "d3dUtil.h"
#include "MathHelper.h"
#include "DirectXBuffers.h"
#include "GameObject.h"
#include "Transform.h"
#include "Renderer.h"




#define MaxLights 16


struct Light
{
    DirectX::XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };
    float FalloffStart = 1.0f;                          // point/spot light only
    DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };// directional/spot light only
    float FalloffEnd = 10.0f;                           // point/spot light only
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  // point/spot light only
    float SpotPower = 64.0f;                            // spot light only
};

struct PassConstants
{
    DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    float tempFloat = 0.0f;
    DirectX::XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
    DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
    float NearZ = 0.0f;
    float FarZ = 0.0f;
    float TotalTime = 0.0f;
    float DeltaTime = 0.0f;
    DirectX::XMFLOAT4 AmbientLight = { 0.0f, 0.0f, 0.0f, 1.0f };


    DirectX::XMFLOAT4 FogColor = { 0.7f, 0.7f, 0.7f, 1.0f };
    float gFogStart = 5.0f;
    float gFogRange = 150.0f;
    DirectX::XMFLOAT2 cbPerObjectPad2;

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MaxLights per object.
    Light Lights[MaxLights];
};

struct Vertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 texCord;
};




class RenderItem : public GameObject
{
    std::string name;
    ID3D12Device* device;
	
public:
    RenderItem(ID3D12Device* device); 


    //Material* Material = nullptr;
    //MeshGeometry* Mesh = nullptr;

    //// Primitive topology.
    //D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    //// DrawIndexedInstanced parameters.
    //UINT IndexCount = 0;
    //UINT StartIndexLocation = 0;
    //int BaseVertexLocation = 0;


    void Update();

    void Draw(ID3D12GraphicsCommandList* cmdList);
};


// Stores the resources needed for the CPU to build the command lists
// for a frame.  
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
   // std::unique_ptr<ConstantBuffer<ObjectConstants>> ObjectConstantBuffer = nullptr;
   // std::unique_ptr<ConstantBuffer<MaterialConstants>> MaterialConstantBuffer	= nullptr;

    // Fence value to mark commands up to this fence point.  This lets us
    // check if these frame resources are still in use by the GPU.
    UINT64 FenceValue = 0;
};