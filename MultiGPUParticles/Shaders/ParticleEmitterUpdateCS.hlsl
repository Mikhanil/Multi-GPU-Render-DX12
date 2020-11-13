#include "Common.hlsl"



[numthreads(64, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint index = ParticlesIndexes.Consume();

    ParticleData particle = Particles[index];

    particle.Velocity += particle.Acceleration * EmitterBuffer.DeltaTime;
    particle.Position += particle.Velocity * EmitterBuffer.DeltaTime;

    Particles[index] = particle;	
}