#define THREADS_X 8
#define THREADS_Y 8

// Edge-preserving bilateral smoothing of the maximal weight map.
// Implements Zhang et al. 2019 (TVCG) Eq. 12:
//   W(p) = sum_q[ F(p,q) * G(Wmax(p), Wmax(q)) * Wmax(q) ]
//          / sum_q[ F(p,q) * G(Wmax(p), Wmax(q)) ]
// where F is a spatial Gaussian and G is a range Gaussian on Wmax values
// (NOT on depth — the filter preserves boundaries in the weight map itself).

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

Texture2D<float> WIn : register(t4);
RWTexture2D<float> WOut : register(u4);

[numthreads(THREADS_X, THREADS_Y, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)Resolution.x || id.y >= (uint)Resolution.y) return;

    const float wCenter = WIn[id.xy];
    const int rad = (int)FilterRadius;

    // Spatial variance: radius/2 gives a full-width half-max near the kernel edge.
    const float spatialSigma = max(FilterRadius * 0.5f, 1.0f);
    const float invSpatial2 = 1.0f / (2.0f * spatialSigma * spatialSigma);

    // Range variance: WMax values live in [0,1]; 0.15 is edge-preserving without
    // over-smoothing flat regions. Tuned empirically against Zhang's Fig. 7.
    const float rangeSigma = 0.15f;
    const float invRange2 = 1.0f / (2.0f * rangeSigma * rangeSigma);

    float accum = 0.0f;
    float totalW = 0.0f;

    for (int y = -rad; y <= rad; ++y)
    {
        for (int x = -rad; x <= rad; ++x)
        {
            int2 coord = int2(id.xy) + int2(x, y);
            if (coord.x < 0 || coord.y < 0 ||
                coord.x >= (int)Resolution.x || coord.y >= (int)Resolution.y) continue;

            const float wN = WIn[coord];
            const float dist2 = float(x * x + y * y);
            const float diff = wN - wCenter;

            const float w = exp(-dist2 * invSpatial2 - diff * diff * invRange2);
            accum += wN * w;
            totalW += w;
        }
    }

    WOut[id.xy] = accum / max(totalW, 1e-4f);
}
