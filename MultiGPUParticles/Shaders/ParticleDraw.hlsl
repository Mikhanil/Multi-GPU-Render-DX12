#include "Common.hlsl"

ConstantBuffer<EmitterData> EmitterBuffer : register(b0, space1);
RWStructuredBuffer<ParticleData> Particles : register(u0);

struct VertexOut
{
    float4 PositionV : SV_Position;
    uint Index : INDEX;
};

VertexOut VS(uint VertexID : SV_VertexID)
{
    VertexOut output = (VertexOut) 0;

    ParticleData particle = Particles[VertexID];

    output.PositionV = mul(float4(particle.Position, 1), objectBuffer.World);
    output.PositionV = mul(output.PositionV, worldBuffer.View);
    output.Index = VertexID;
    return output;
}

struct GeoOut
{
    float4 PositionH : SV_POSITION;
    float4 PositionV : POSITION;
    float3 NormalW : NORMAL;
    float2 UV : TEXCOORD;
    uint Index : INDEX;
};

// функция изменения вертекса и последующая проекция его в Projection Space
GeoOut _offsetNprojected(VertexOut data, float2 offset, float2 uv)
{
    GeoOut o = (GeoOut)0;
    o.PositionV = data.PositionV;
	
    o.PositionV.xy += offset;
    o.PositionH = mul(o.PositionV, worldBuffer.Proj);
    o.UV = uv;
    o.Index = data.Index;
	
    return o;
}


// We expand each point into a quad (4 vertices), so the maximum number of vertices
// we output per geometry shader invocation is 4.
[maxvertexcount(4)]
void GS(point VertexOut gin[1], inout TriangleStream<GeoOut> triStream)
{
    const float size = EmitterBuffer.Size; // размер конченого квадрата
    // описание квадрата
    triStream.Append(_offsetNprojected(gin[0], float2(-1, -1) * size, float2(0, 0)));
    triStream.Append(_offsetNprojected(gin[0], float2(-1, 1) * size, float2(0, 1)));
    triStream.Append(_offsetNprojected(gin[0], float2(1, -1) * size, float2(1, 0)));
    triStream.Append(_offsetNprojected(gin[0], float2(1, 1) * size, float2(1, 1)));	
	

    triStream.RestartStrip();
}

float4 PS(GeoOut pin) : SV_Target
{
    return EmitterBuffer.Color;
}