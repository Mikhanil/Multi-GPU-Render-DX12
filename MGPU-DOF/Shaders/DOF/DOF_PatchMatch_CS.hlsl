#define THREADS_X 8
#define THREADS_Y 8

cbuffer DOFParamsA : register(b0)
{
    float2 Resolution;
    float FocusDistance;
    float FocusRange;
}

cbuffer DOFParamsB : register(b1)
{
    float MaxCoC;
    float FilterRadius;
}

Texture2D<float4> ColorTex : register(t1);
Texture2D<uint> OcclusionMask : register(t2);
Texture2D<float4> Pyramid1 : register(t7);
Texture2D<float4> Pyramid2 : register(t8);
RWTexture2D<float4> OutBase : register(u2);
RWTexture2D<float4> OutL1 : register(u6);
RWTexture2D<float4> OutL2 : register(u7);

cbuffer PatchParams : register(b2)
{
    uint Level; // 0 base, 1 level1, 2 level2
}

static const uint kIterations = 4;

[numthreads(THREADS_X, THREADS_Y, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    // Compute effective resolution for this level
    uint2 levelRes = uint2(Resolution);
    uint levelShift = Level;
    levelRes.x = max(1u, levelRes.x >> levelShift);
    levelRes.y = max(1u, levelRes.y >> levelShift);

    if (id.x >= levelRes.x || id.y >= levelRes.y) return;

    // Map to full-res coordinates for OcclusionMask sampling
    uint2 fullCoord = id.xy << levelShift;
    fullCoord = min(fullCoord, uint2(Resolution) - 1);

    if (OcclusionMask[fullCoord] == 0)
    {
        // Not occluded — pass through original color
        float4 color = ColorTex[min(id.xy, uint2(Resolution) - 1)];
        if (Level == 0) OutBase[id.xy] = color;
        else if (Level == 1) OutL1[id.xy] = color;
        else OutL2[id.xy] = color;
        return;
    }

    // Choose coarse hint based on level
    float4 best = 0;
    if (Level == 2)
    {
        best = ColorTex[min(id.xy, uint2(Resolution) - 1)];
    }
    else if (Level == 1)
    {
        uint2 dim;
        Pyramid2.GetDimensions(dim.x, dim.y);
        best = Pyramid2[min(id.xy, dim - 1)];
    }
    else
    {
        uint2 dim;
        Pyramid1.GetDimensions(dim.x, dim.y);
        best = Pyramid1[min(id.xy, dim - 1)];
    }

    uint2 seed = uint2(id.xy);
    int maxRadius = 8;
    for (uint iter = 0; iter < kIterations; ++iter)
    {
        int radius = maxRadius >> iter;
        radius = max(1, radius);

        // Propagation: check 4 neighbors
        int2 offsets[4] = {int2(-1,0), int2(1,0), int2(0,-1), int2(0,1)};
        [unroll]
        for (int i = 0; i < 4; ++i)
        {
            int2 c = int2(id.xy) + offsets[i];
            if (c.x < 0 || c.y < 0 || c.x >= (int)levelRes.x || c.y >= (int)levelRes.y) continue;
            uint2 cFull = min(uint2(c) << levelShift, uint2(Resolution) - 1);
            if (OcclusionMask[cFull] == 0)
            {
                best = ColorTex[min(uint2(c), uint2(Resolution) - 1)];
                radius = max(1, radius / 2);
                break;
            }
        }

        // Random search
        for (int s = 0; s < 4; ++s)
        {
            int2 rnd = int2(((seed.x * 1664525 + 1013904223) >> 16) & 31,
                            ((seed.y * 22695477 + 1) >> 16) & 31);
            seed += rnd + s;
            int2 offset = (rnd % (2 * radius + 1)) - radius;
            int2 sampleCoord = int2(id.xy) + offset;
            sampleCoord = clamp(sampleCoord, int2(0,0), int2(levelRes) - 1);
            uint2 sFull = min(uint2(sampleCoord) << levelShift, uint2(Resolution) - 1);
            if (OcclusionMask[sFull] == 0)
            {
                best = ColorTex[min(uint2(sampleCoord), uint2(Resolution) - 1)];
                break;
            }
        }
    }

    if (Level == 0) OutBase[id.xy] = best;
    else if (Level == 1) OutL1[id.xy] = best;
    else OutL2[id.xy] = best;
}
