#include "Common.hlsl"

TextureCube ReflectionProbe0 : register(t0, space3);

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION0;
    float3 NormalW : NORMAL;
};

VertexOut REFLECTIONS_VS(VertexIn vin)
{
    VertexOut vout;
    float4 posW = mul(float4(vin.PosL, 1.0f), objectBuffer.World);
    vout.PosW = posW.xyz;
    vout.NormalW = normalize(mul(vin.NormalL, (float3x3)objectBuffer.World));
    vout.PosH = mul(posW, worldBuffer.ViewProj);
    return vout;
}

float4 REFLECTIONS_PS(VertexOut pin) : SV_Target
{
    float3 toEyeW = normalize(worldBuffer.EyePosW - pin.PosW);
    float3 reflectionDirection = reflect(-toEyeW, normalize(pin.NormalW));
    return ReflectionProbe0.Sample(gsamLinearWrap, reflectionDirection);
}