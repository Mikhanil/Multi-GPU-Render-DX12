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
    if (worldBuffer.debugMap >= 2)	
        return float4(ssaoMap.Sample(gsamLinearWrap, pin.TexC).rrr, 1.0f);

    if (worldBuffer.debugMap >= 1)	
        return float4(shadowMap.Sample(gsamLinearWrap, pin.TexC).rrr, 1.0f);
	
    return ssaoMap.Sample(gsamLinearWrap, pin.TexC);
	
    if (worldBuffer.debugMap >= 2)	
		return float4(ssaoMap.Sample(gsamLinearWrap, pin.TexC).rrr, 1.0f);

    if (worldBuffer.debugMap >= 1)	
        return float4(shadowMap.Sample(gsamLinearWrap, pin.TexC).rrr, 1.0f);
	
    discard;

	//Шейдер не компилируется если нету тут return, который все равно никогда не будет достигнут
    return float4(0, 0, 0, 0);
}

