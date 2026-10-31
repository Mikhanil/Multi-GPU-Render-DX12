#include "Common.hlsl"

ConstantBuffer<EmitterData> EmitterBuffer : register(b0, space1);
StructuredBuffer<ParticleData> Particles : register(t0, space1);
StructuredBuffer<uint> RenderingParticles : register(t1, space1);
Texture2D Atlas[] : register(t2, space1);

struct VertexOut
{
    float4 PositionV : SV_Position;
    uint ParticleIndex : INDEX;
    uint TextureIndex : INDEX1;
};

VertexOut VS(uint VertexID : SV_VertexID)
{
    uint index = RenderingParticles[VertexID];

    ParticleData particle = Particles[index];

    VertexOut output = (VertexOut)0;
    output.PositionV = mul(float4(particle.Position, 1), objectBuffer.World);
    output.PositionV = mul(output.PositionV, worldBuffer.View);
    output.ParticleIndex = index;
    output.TextureIndex = particle.TextureIndex;
    return output;
}

struct GeoOut
{
    float4 PositionH : SV_POSITION;
    float4 PositionV : POSITION;
    float3 NormalW : NORMAL;
    float2 UV : TEXCOORD;
    uint ParticleIndex : INDEX;
    uint TextureIndex : INDEX1;
};

// ������� ��������� �������� � ����������� �������� ��� � Projection Space
GeoOut CreateQuadVertex(VertexOut data, float2 offset, float2 uv)
{
    GeoOut o = (GeoOut)0;
    o.PositionV = data.PositionV;

    o.PositionV.xy += offset;
    o.PositionH = mul(o.PositionV, worldBuffer.Proj);
    o.UV = uv;
    o.ParticleIndex = data.ParticleIndex;
    o.TextureIndex = data.TextureIndex;
    return o;
}


// We expand each point into a quad (4 vertices), so the maximum number of vertices
// we output per geometry shader invocation is 4.
[maxvertexcount(4)]
void GS(point VertexOut gin[1], inout TriangleStream<GeoOut> triStream)
{
    triStream.Append(CreateQuadVertex(gin[0], float2(-1, -1) * EmitterBuffer.Size, float2(0, 0)));
    triStream.Append(CreateQuadVertex(gin[0], float2(-1, 1) * EmitterBuffer.Size, float2(0, 1)));
    triStream.Append(CreateQuadVertex(gin[0], float2(1, -1) * EmitterBuffer.Size, float2(1, 0)));
    triStream.Append(CreateQuadVertex(gin[0], float2(1, 1) * EmitterBuffer.Size, float2(1, 1)));
    triStream.RestartStrip();
}

float4 PS(GeoOut pin) : SV_Target
{
    if (EmitterBuffer.UseTexture)
    {
        return Atlas[pin.TextureIndex].Sample(gsamAnisotropicWrap, pin.UV);
    }

    float intensity = 0.5f - length(float2(0.5f, 0.5f) - pin.UV);
    intensity = clamp(intensity, 0.0f, 0.5f) * 2.0f;

    return float4(EmitterBuffer.Color.xyz, intensity);
}
