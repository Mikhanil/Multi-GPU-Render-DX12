// Copyright Epic Games, Inc. All Rights Reserved.

#include "Common.hlsl"

struct PPVertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

PPVertexOut PostProcessVS(uint vertexID : SV_VertexID)
{
    PPVertexOut output = (PPVertexOut) 0;
	
    float2 pos;
    float2 uv;

    if (vertexID == 0)
    {
        pos = float2(-1.0, -1.0);
        uv = float2(0.0, 1.0);
    }
    else if (vertexID == 1)
    {
        pos = float2(-1.0, 3.0);
        uv = float2(0.0, -1.0);
    }
    else // vertexID == 2
    {
        pos = float2(3.0, -1.0);
        uv = float2(2.0, 1.0);
    }

    output.PosH = float4(pos, 1.0f, 1.0f);
    output.TexC = uv;
    return output;
}

float4 PostProcessPS(PPVertexOut input) : SV_TARGET
{
    uint2 texCoord = input.PosH.xy / worldBuffer.SsaaMultilpier;

    float4 rgbA = rayMarchingRes.Load(uint3(texCoord, 0));
    
	rgbA /= rgbA.aaaa;	// Normalize according to sample count when path tracing
    
	float3 white_point = float3(1.08241, 0.96756, 0.95003);
	float exposure = 10.0;
    
    return float4(pow((float3) 1.0 - exp(-rgbA.rgb / white_point * exposure), (float3) (1.0 / 2.2)), 1.0);
}
