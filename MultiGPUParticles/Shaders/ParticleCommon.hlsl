struct ParticleData
{
    float3 Position;
    float3 Velocity;
    float Acceleration;
    float Padding;
};

struct EmitterData
{
    float4 Color;
    float DeltaTime;
    float TotalTime;
    float Size;
    int ParticlesCount;
};
