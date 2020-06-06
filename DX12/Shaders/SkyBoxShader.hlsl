
#include "Common.hlsl"

TextureCube SkyMap : register(t0);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerState gsamCubeTextureWrap : register(s6);



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