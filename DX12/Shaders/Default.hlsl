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

Texture2D gDiffuseMap : register(t0);


SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

struct ObjectData
{
    float4x4 World;
    float4x4 TexTransform;
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

// Constant data that varies per material.
struct MaterialData
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Roughness;
    float4x4 MatTransform;
};

ConstantBuffer<MaterialData> materialBuffer : register(b2);


struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosView : SV_POSITION;
    float3 PosWorld : POSITION;
    float3 NormalWorld : NORMAL;
    float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;
	
    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f),objectBuffer.World);
    vout.PosWorld = posW.xyz;
     
    vout.NormalWorld = mul(vin.NormalL, (float3x3) objectBuffer.World);

    vout.PosView = mul(posW, worldBuffer.ViewProj);
	
    
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), objectBuffer.TexTransform);
    vout.TexC = mul(texC, materialBuffer.MatTransform).xy;
	
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    float4 diffuseAlbedo = gDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC) 
    // * gSecondDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC)
    * materialBuffer.DiffuseAlbedo;
	
#ifdef ALPHA_TEST	
	clip(diffuseAlbedo.a - 0.1f);
#endif
        
    pin.NormalWorld = normalize(pin.NormalWorld);

    // Vector from point being lit to eye. 
    float3 toEyeW = normalize(worldBuffer.EyePosW - pin.PosWorld);
    float distToEye = length(toEyeW);
    toEyeW /= distToEye;

    // Light terms.
    float4 ambient = worldBuffer.AmbientLight * diffuseAlbedo;

    const float shininess = 1.0f - materialBuffer.Roughness;
    Material mat = { diffuseAlbedo, materialBuffer.FresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(worldBuffer.Lights, mat, pin.PosWorld,
        pin.NormalWorld, toEyeW, shadowFactor);

    float4 litColor = ambient + directLight;

#ifdef FOG
	float fogAmount = saturate((distToEye - worldBuffer.gFogStart) / worldBuffer.gFogRange);
    litColor = lerp(litColor, worldBuffer.gFogColor, fogAmount);
#endif    
    
    litColor.a = diffuseAlbedo.a;

    return litColor;
}


