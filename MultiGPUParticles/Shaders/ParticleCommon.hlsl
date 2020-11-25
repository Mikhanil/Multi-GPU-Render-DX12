struct ParticleData
{
    float3 Position;
    float3 Velocity;
    float LifeTime;
};

struct EmitterData
{
    float4 Color;

	float3 Force;    
    float DeltaTime;
	
    float2 Size;
    uint ParticlesTotalCount;	
    uint SimulatedGroupCount;
	
    uint ParticleInjectCount;
    uint InjectGroupCount;
    uint ParticleAliveCount;	
    float Acceleration;
};
