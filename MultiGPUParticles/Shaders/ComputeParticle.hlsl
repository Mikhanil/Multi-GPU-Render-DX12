#include "ParticleCommon.hlsl"

struct ParticleIndex
{
    uint index;
};


ConstantBuffer<EmitterData> EmitterBuffer : register(b0);
RWStructuredBuffer<ParticleData> Particles : register(u0);
ConsumeStructuredBuffer<uint> ParticlesIndexes : register(u1);

static float deltaTime = 1 / 60;

[numthreads(32, 1, 1)]
void CS(uint3 DTid : SV_DispatchThreadID)
{
    uint index = ParticlesIndexes.Consume();

    ParticleData particle = Particles[index];

    particle.Velocity += particle.Acceleration * deltaTime;
    particle.Position += particle.Velocity * deltaTime;

    Particles[index] = particle;
}
