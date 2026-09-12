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
    uint Stage;
    uint Step;
    uint SourceStride;
    uint Expanded;
};

ByteAddressBuffer Source : register(t0);
RWStructuredBuffer<uint2> SortedIndices : register(u0);

[numthreads(256, 1, 1)]
void CS_Initialize(uint3 tid : SV_DispatchThreadID)
{
    uint i = tid.x;
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
    SortedIndices[i] = uint2(key, i < Count ? i : 0xffffffffu);
}

bool Greater(uint2 a, uint2 b)
{
    return a.x > b.x || (a.x == b.x && a.y > b.y);
}

[numthreads(256, 1, 1)]
void CS_Bitonic(uint3 tid : SV_DispatchThreadID)
{
    uint i = tid.x;
    uint partner = i ^ Step;
    if (i >= Capacity || partner <= i || partner >= Capacity)
        return;
    uint2 a = SortedIndices[i];
    uint2 b = SortedIndices[partner];
    bool ascending = (i & Stage) == 0u;
    if (ascending ? Greater(a, b) : Greater(b, a))
    {
        SortedIndices[i] = b;
        SortedIndices[partner] = a;
    }
}
