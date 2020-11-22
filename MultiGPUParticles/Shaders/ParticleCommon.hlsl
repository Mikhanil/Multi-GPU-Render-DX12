struct ParticleData
{
    float3 Position;
    float3 Velocity;
    float Acceleration;
    float LifeTime;
};

struct EmitterData
{
    float4 Color;

	float3 Force;    
    float DeltaTime;
	
    float2 Size;
    uint ParticlesCount;	
    uint SimulatedGroupCount;
	
    uint ParticleInjectCount;
    uint InjectGroupCount;
    uint Padding3;
    uint Padding4;
};
