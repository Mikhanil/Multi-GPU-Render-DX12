cbuffer ObjectConstants : register(b0)
{
    float4x4 World;
};

// Prefix of the shared WorldConstants buffer used by GrassDraw.hlsl.
cbuffer WorldConstants : register(b1)
{
    float4x4 View;
    float4x4 InvView;
    float4x4 Proj;
    float4x4 InvProj;
    float4x4 ViewProj;
    float4x4 InvViewProj;
    float4x4 ViewProjTex;
    float4x4 ShadowTransform;
    float3 EyePosW;
};

cbuffer SortConstants : register(b2)
{
    uint Count;
    uint Capacity;
    uint SourceStride;
    uint Expanded;
};

ByteAddressBuffer Source : register(t0);
RWByteAddressBuffer SortedIndices : register(u0);
RWByteAddressBuffer SortCounter : register(u1);

[numthreads(256, 1, 1)]
void CS_Initialize(uint3 tid : SV_DispatchThreadID)
{
    uint i = tid.x;
    if (i == 0)
        SortCounter.Store(0, Capacity);
    if (i >= Capacity)
        return;
    uint key = 0xffffffffu;
    if (i < Count)
    {
        uint address = i * SourceStride;
        // ExtraPad0 at byte 36 stores the active vertex count of an expanded instance.
        bool visible = true;
        if (Expanded != 0)
            visible = asfloat(Source.Load(address + 36u)) > 0.0f;
        if (visible)
        {
            float3 position = asfloat(Source.Load3(address));
            float3 delta = mul(float4(position, 1.0f), World).xyz - EyePosW;
            float distanceSquared = dot(delta, delta);
            if (isfinite(distanceSquared))
                key = asuint(distanceSquared); // Nonnegative floats preserve order as uint.
        }
    }
    // MiniEngine's 64-bit layout is index in the low word, sort key in the high word.
    SortedIndices.Store2(i * 8u, uint2(i < Count ? i : 0xffffffffu, key));
}
