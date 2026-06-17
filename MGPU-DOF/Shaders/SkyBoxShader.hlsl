#include "Common.hlsl"


struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosL : POSITION;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};


VertexOut SKYMAP_VS(VertexIn vin)
{
    VertexOut vout;

    // Use local vertex position as cubemap lookup vector.
    vout.PosL = vin.PosL;

    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), objectBuffer.World);

    // Always center sky about camera.
    posW.xyz += worldBuffer.EyePosW;

    // Set z = w so that z/w = 1 (i.e., skydome always on far plane).
    vout.PosH = mul(posW, worldBuffer.ViewProj).xyww;

    return vout;
}

float4 SKYMAP_PS(VertexOut pin) : SV_Target
{
    return SkyMap.Sample(gsamLinearWrap, pin.PosL);
}
