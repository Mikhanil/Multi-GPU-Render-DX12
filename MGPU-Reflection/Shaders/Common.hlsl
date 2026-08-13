// Structured lighting ABI for MGPU-Reflection.
#include "LightingUtil.hlsl"
#include "ParticleCommon.hlsl"

struct ObjectData
{
    float4x4 World;
    float4x4 TexTransform;
    uint materialIndex;
    uint gObjPad0;
    uint gObjPad1;
    uint gObjPad2;
};

struct WorldData
{
    float4x4 View;
    float4x4 InvView;
    float4x4 Proj;
    float4x4 InvProj;
    float4x4 ViewProj;
    float4x4 InvViewProj;
    float4x4 ViewProjTex;
    float4x4 ShadowTransform;
    float3 EyePosW;
    float debugMap;
    float2 RenderTargetSize;
    float2 InvRenderTargetSize;
    float NearZ;
    float FarZ;
    float TotalTime;
    float DeltaTime;
    float4 AmbientLight;
    float3 CameraForwardVector;
    float SsaaMultilpier;
    Light DirectionalLight;
    uint PointLightCount;
    uint SpotLightCount;
    uint2 LightCountPad;
};

struct MaterialData
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Roughness;
    float4x4 MatTransform;
    uint DiffuseMapIndex;
    uint NormalMapIndex;
    uint MatPad1;
    uint MatPad2;
};

ConstantBuffer<ObjectData> objectBuffer : register(b0);
ConstantBuffer<WorldData> worldBuffer : register(b1);
StructuredBuffer<MaterialData> materialData : register(t0, space1);
StructuredBuffer<Light> pointLightData : register(t1, space1);
StructuredBuffer<Light> spotLightData : register(t2, space1);

TextureCube SkyMap : register(t0);
Texture2D shadowMap : register(t1);
Texture2D ssaoMap : register(t2);
Texture2D texturesMaps[] : register(t3);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerComparisonState gsamShadow : register(s6);

float3 NormalSampleToWorldSpace(float3 normalMapSample, float3 unitNormalW, float3 tangentW)
{
    float3 normalT = 2.0f * normalMapSample - 1.0f;
    float3 N = unitNormalW;
    float3 T = normalize(tangentW - dot(tangentW, N) * N);
    float3 B = cross(N, T);
    return mul(normalT, float3x3(T, B, N));
}

float CalcShadowFactor(float4 shadowPosH)
{
    shadowPosH.xyz /= shadowPosH.w;
    uint width, height, numMips;
    shadowMap.GetDimensions(0, width, height, numMips);
    const float dx = 1.0f / (float)width;
    const float2 offsets[9] =
    {
        float2(-dx, -dx), float2(0.0f, -dx), float2(dx, -dx),
        float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
        float2(-dx, dx), float2(0.0f, dx), float2(dx, dx)
    };
    float percentLit = 0.0f;
    [unroll]
    for (int i = 0; i < 9; ++i)
        percentLit += shadowMap.SampleCmpLevelZero(gsamShadow, shadowPosH.xy + offsets[i], shadowPosH.z).r;
    return percentLit / 9.0f;
}
