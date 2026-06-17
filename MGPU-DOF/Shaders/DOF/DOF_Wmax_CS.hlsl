#define THREADS_X 8
#define THREADS_Y 8

// Wmax estimation (Zhang 2019 Eq. 11): ratio of blocked pixels to total
// pixels inside p's Circle-of-Confusion. Uses a fixed 32-tap sunflower
// (golden-angle) Poisson disk instead of a dense O(r^2) square scan.
// Each pixel gets a per-pixel rotation (hash of coords) to decorrelate
// stepped-CoC artifacts.

cbuffer DOFParamsA : register(b0)
{
    float2 Resolution;
    float FocusDistance;
    float FocusRange;
}

cbuffer DOFParamsB : register(b1)
{
    float MaxCoC;
    float FilterRadius; // unused here but kept aligned with root signature
    float DofTapsF;     // host sends int as float; cast to uint at use
}

Texture2D<uint> OcclusionMask : register(t2);
Texture2D<float> CoC : register(t6);
RWTexture2D<float> WMax : register(u3);

static const float kGoldenAngle = 2.39996323f;

float2 SunflowerSample(uint i, uint kTaps, float2 sinCosRot)
{
    const float r = sqrt((float(i) + 0.5f) / float(kTaps));
    const float theta = float(i) * kGoldenAngle;
    float s, c;
    sincos(theta, s, c);
    return float2(c * sinCosRot.x - s * sinCosRot.y,
                  c * sinCosRot.y + s * sinCosRot.x) * r;
}

[numthreads(THREADS_X, THREADS_Y, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)Resolution.x || id.y >= (uint)Resolution.y) return;

    const float cocP = CoC[id.xy];
    if (cocP < 0.5f)
    {
        WMax[id.xy] = 0.0f;
        return;
    }

    const float radius = min(cocP, MaxCoC);

    // Per-pixel rotation: cheap integer hash → [0, 2π).
    const uint h = (id.x * 1664525u + 1013904223u) ^ (id.y * 22695477u);
    const float rot = float(h & 0xFFFFu) * (6.2831853f / 65536.0f);
    float sR, cR;
    sincos(rot, sR, cR);
    const float2 sinCosRot = float2(cR, sR);

    const uint kTaps = max((uint)DofTapsF, 4u);

    uint occluded = 0;
    uint total = 0;

    [loop]
    for (uint i = 0; i < kTaps; ++i)
    {
        const float2 tap = SunflowerSample(i, kTaps, sinCosRot) * radius;
        const int2 coord = int2(id.xy) + int2(round(tap));
        if (coord.x < 0 || coord.y < 0 ||
            coord.x >= (int)Resolution.x || coord.y >= (int)Resolution.y) continue;

        if (OcclusionMask[coord] != 0)
            occluded++;
        total++;
    }

    WMax[id.xy] = (total > 0) ? (float)occluded / (float)total : 0.0f;
}
