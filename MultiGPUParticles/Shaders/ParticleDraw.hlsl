#include "Common.hlsl"

struct PixelInput
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
};

PixelInput VS(uint VertexID : SV_VertexID)
{
    PixelInput output = (PixelInput) 0;

    ParticleData particle = Particles[VertexID];

    float4 worldPosition = float4(particle.Position, 1);
    float4 viewPosition = mul(worldPosition, worldBuffer.View);
    output.Position = mul(viewPosition, worldBuffer.Proj);
    output.Color = particle.Color;
    return output;
}

float4 PS(PixelInput pin) : SV_Target
{	
    return pin.Color;
}