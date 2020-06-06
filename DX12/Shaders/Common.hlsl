// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
#define NUM_SPOT_LIGHTS 0
#endif

// Include structures and functions for lighting.
#include "LightingUtil.hlsl"

struct ObjectData
{
    float4x4 World;
    float4x4 TexTransform;
    uint materialIndex;
    uint gObjPad0;
    uint gObjPad1;
    uint gObjPad2;
};

ConstantBuffer<ObjectData> objectBuffer : register(b0);

struct WorldData
{
    float4x4 View;
    float4x4 InvView;
    float4x4 Proj;
    float4x4 InvProj;
    float4x4 ViewProj;
    float4x4 InvViewProj;
    float3 EyePosW;
    float cbPerObjectPad1;
    float2 RenderTargetSize;
    float2 InvRenderTargetSize;
    float NearZ;
    float FarZ;
    float TotalTime;
    float DeltaTime;
    float4 AmbientLight;

    float4 gFogColor;
    float gFogStart;
    float gFogRange;
    float2 cbPerObjectPad2;
    
    
    Light Lights[MaxLights];
};

ConstantBuffer<WorldData> worldBuffer : register(b1);

struct MaterialData
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Roughness;
    float4x4 MatTransform;
    uint DiffuseMapIndex;
    uint MatPad0;
    uint MatPad1;
    uint MatPad2;
};

// Put in space1, so the texture array does not overlap with these resources.  
// The texture array will occupy registers t0, t1, ..., t3 in space0. 
StructuredBuffer<MaterialData> materialData : register(t0, space1);

//ConstantBuffer<MaterialData> materialBuffer : register(b2);