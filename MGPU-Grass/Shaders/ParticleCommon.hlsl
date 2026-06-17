struct ParticleData
{
    float3 Position;
    float LifeTime;

    float3 Velocity;
    float TotalLifeTime;

    uint TextureIndex;
    uint PD1;
    uint PD2;
    uint PD3;
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
    uint AtlasTextureCount;

    bool UseTexture;
    bool Padding;
    bool Padding1;
    bool Padding2;
    float3 Padding3;
};
