#include "ParticleCommon.hlsl"

ConstantBuffer<EmitterData> EmitterBuffer : register(b0);
RWStructuredBuffer<ParticleData> ParticlesPool : register(u0);


//#define INJECTION
//#define SIMULATION

#ifdef INJECTION
ConsumeStructuredBuffer<uint>		DeadParticles	: register(u1);
AppendStructuredBuffer<uint> AliveParticles : register(u2);
RWStructuredBuffer<ParticleData> InjectionParticles : register(u3);
#endif

#ifdef SIMULATION
AppendStructuredBuffer<uint> DeadParticles : register(u1);
ConsumeStructuredBuffer<uint> AliveParticles : register(u2);
AppendStructuredBuffer<uint> RenderParticles : register(u3);
#endif


#define THREAD_GROUP_X 32
#define THREAD_GROUP_Y 24
#define THREAD_GROUP_TOTAL 768

[numthreads(THREAD_GROUP_X, THREAD_GROUP_Y, 1)]
void CS(uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex)
{	
     	
#ifdef INJECTION

	 uint threadParticleIndex = groupID.x * THREAD_GROUP_TOTAL + groupID.y * EmitterBuffer.InjectGroupCount * THREAD_GROUP_TOTAL + groupIndex;

	
    [flatten]
    if (threadParticleIndex >= EmitterBuffer.ParticleInjectCount)
        return;
	
    uint particleIndex = DeadParticles.Consume();
    ParticleData injectParticle = InjectionParticles[threadParticleIndex];
    ParticlesPool[particleIndex] = injectParticle;
    	
    AliveParticles.Append(particleIndex);
	return;
#endif

#ifdef SIMULATION

	 uint threadParticleIndex = groupID.x * THREAD_GROUP_TOTAL + groupID.y * EmitterBuffer.SimulatedGroupCount * THREAD_GROUP_TOTAL + groupIndex;

	
    [flatten]
    if (threadParticleIndex >= EmitterBuffer.ParticlesCount)
        return;
	
	uint aliveIndex = AliveParticles.Consume();
    ParticleData particle = ParticlesPool[aliveIndex];

    particle.LifeTime -= EmitterBuffer.DeltaTime;

    [branch]
    if (particle.LifeTime <= 0)
    {
        DeadParticles.Append(aliveIndex);
        return;
    }

    particle.Velocity -= EmitterBuffer.Force * EmitterBuffer.DeltaTime;
    particle.Position += particle.Velocity * EmitterBuffer.DeltaTime;

	ParticlesPool[aliveIndex] = particle;
	RenderParticles.Append(aliveIndex);
#endif

    

	
	
}

