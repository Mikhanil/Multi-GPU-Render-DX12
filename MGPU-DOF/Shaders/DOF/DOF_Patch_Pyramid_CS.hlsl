#define THREADS_X 8
#define THREADS_Y 8

cbuffer DOFParamsA : register(b0)
{
    float2 Resolution;
    float FocusDistance;
    float FocusRange;
}

cbuffer PatchParams : register(b2)
{
    uint PyramidLevel; // 0 = build level1 from color, 1 = build level2 from level1
}

Texture2D<float4> ColorTex : register(t1);
Texture2D<uint> OcclusionMask : register(t2);
Texture2D<float4> Pyramid1In : register(t7);  // for level2 pass: read from level1
RWTexture2D<float4> OutLevel1 : register(u6);
RWTexture2D<float4> OutLevel2 : register(u7);

[numthreads(THREADS_X, THREADS_Y, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (PyramidLevel == 0)
    {
        // Build level1 (half res) from full-res color
        uint2 dim0;
        ColorTex.GetDimensions(dim0.x, dim0.y);
        uint2 dim1;
        OutLevel1.GetDimensions(dim1.x, dim1.y);

        if (id.x >= dim1.x || id.y >= dim1.y) return;

        int2 base = int2(id.xy) * 2;
        float4 accum = 0;
        uint count = 0;
        for (int dy = 0; dy < 2; ++dy)
        {
            for (int dx = 0; dx < 2; ++dx)
            {
                int2 c = base + int2(dx, dy);
                if (c.x >= (int)dim0.x || c.y >= (int)dim0.y) continue;
                if (OcclusionMask[c] == 0)
                {
                    accum += ColorTex[c];
                    count++;
                }
            }
        }
        OutLevel1[id.xy] = (count > 0) ? accum / count : ColorTex[clamp(base, int2(0,0), int2(dim0) - 1)];
    }
    else
    {
        // Build level2 (quarter res) from level1
        uint2 dim1;
        Pyramid1In.GetDimensions(dim1.x, dim1.y);
        uint2 dim2;
        OutLevel2.GetDimensions(dim2.x, dim2.y);

        if (id.x >= dim2.x || id.y >= dim2.y) return;

        int2 base = int2(id.xy) * 2;
        float4 accum = 0;
        uint count = 0;
        for (int dy = 0; dy < 2; ++dy)
        {
            for (int dx = 0; dx < 2; ++dx)
            {
                int2 c = base + int2(dx, dy);
                if (c.x >= (int)dim1.x || c.y >= (int)dim1.y) continue;
                accum += Pyramid1In[c];
                count++;
            }
        }
        OutLevel2[id.xy] = accum / max(1u, count);
    }
}
