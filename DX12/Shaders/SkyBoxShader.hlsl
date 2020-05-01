
TextureCube SkyMap : register(t0);
SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerState gsamCubeTextureWrap : register(s6);


struct ObjectData
{
    float4x4 World;
    float4x4 TexTransform;
};

ConstantBuffer<ObjectData> objectBuffer : register(b0);

struct Light
{
    float3 Strength;
    float FalloffStart; // point/spot light only
    float3 Direction; // directional/spot light only
    float FalloffEnd; // point/spot light only
    float3 Position; // point light only
    float SpotPower; // spot light only
};

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
    
    
    Light Lights[16];
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


struct SkyMapOut
{
    float4 Pos : SV_POSITION;
    float3 texCoord : TEXCOORD;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};


SkyMapOut SKYMAP_VS(VertexIn vin)
{
    SkyMapOut output = (SkyMapOut) 0;

    //Set Pos to xyww instead of xyzw, so that z will always be 1 (furthest from camera)
    output.Pos = mul(mul(float4(vin.PosL, 1.0f), objectBuffer.World), worldBuffer.ViewProj).xyww;

    output.texCoord = vin.PosL;

    return output;
}

float4 SKYMAP_PS(SkyMapOut input) : SV_Target
{
    return SkyMap.Sample(gsamCubeTextureWrap, input.texCoord);
}