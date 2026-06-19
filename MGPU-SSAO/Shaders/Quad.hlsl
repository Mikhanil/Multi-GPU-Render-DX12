#include "Common.hlsl"

struct VertexIn
{
    float3 PosL : POSITION;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut)0.0f;

    vout.PosH = float4(vin.PosL, 1.0f);
    vout.TexC = vin.TexC;

    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    float4 c = ssaoMap.Sample(gsamLinearWrap, pin.TexC);
    // Single-channel AO (e.g. R16_UNORM) often returns (r,0,0); replicate to RGB for correct grayscale preview.
    if (c.g * c.g + c.b * c.b < 1e-8)
        return float4(c.r, c.r, c.r, 1.0f);
    return c;
}
