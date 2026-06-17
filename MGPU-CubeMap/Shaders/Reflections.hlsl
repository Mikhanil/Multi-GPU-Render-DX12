#include "Common.hlsl"

struct VertexIn
{
    float3 PosL : POSITION;
    float2 UV : TEXCOORD;
};

struct VertexOut
{
    float4 PosC : SV_POSITION;
    float3 PosW : POSITION0;
    float3 PosL : POSITION1;
    float2 UV : TEXCOORD;
};

VertexOut REFLECTIONS_VS(VertexIn vin)
{
    VertexOut vout;
    vout.PosL = vin.PosL;
    
    float4 posW = mul(float4(vin.PosL, 1.0f), objectBuffer.World);
    vout.PosW = posW;
    
    vout.PosC = mul(posW, worldBuffer.ViewProj);
    
    vout.UV = vin.UV;

    return vout;
}

float4 REFLECTIONS_PS(VertexOut pin) : SV_Target
{
    //return float4(1, 0, 0, 1);
    return SkyMap.Sample(gsamLinearWrap, pin.PosL);
}