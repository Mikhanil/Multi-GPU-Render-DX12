#define THREADS_X 8
#define THREADS_Y 8

// Final DOF composite (Zhang 2019 Eq. 10 / 13):
//   I(p) = (1 - W(p)) * avg[ColorTex[q]] + W(p) * avg[ColorFilled[q]]
// where q ranges over p's CoC disk. We sample the disk with 32 taps
// using a sunflower (golden-angle) Poisson pattern, rotated per-pixel
// to hide stepping on uniform-CoC regions.

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
    float DofTapsF;
}

Texture2D<float4> ColorTex : register(t1);
Texture2D<float4> ColorFilled : register(t3);
Texture2D<float> WFiltered : register(t5);
Texture2D<float> CoC : register(t6);

RWTexture2D<float4> OutDOF : register(u5);

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

// Piecewise sRGB encoding (IEC 61966-2-1). DOFResult is R8G8B8A8_UNORM
// (UAV doesn't allow _SRGB), and the swap chain is in sRGB color space,
// so we encode here to avoid a washed-out presented image.
float LinearChannelToSrgb(float c)
{
    if (c <= 0.0031308f)
        return c * 12.92f;
    return 1.055f * pow(max(c, 0.0f), 1.0f / 2.4f) - 0.055f;
}

float3 LinearToSrgb(float3 c)
{
    return float3(LinearChannelToSrgb(c.r),
                  LinearChannelToSrgb(c.g),
                  LinearChannelToSrgb(c.b));
}

[numthreads(THREADS_X, THREADS_Y, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)Resolution.x || id.y >= (uint)Resolution.y) return;

    const float cocP = CoC[id.xy];
    if (cocP < 0.5f)
    {
        const float4 c = ColorTex[id.xy];
        OutDOF[id.xy] = float4(LinearToSrgb(saturate(c.rgb)), c.a);
        return;
    }

    const float radius = min(cocP, MaxCoC);

    const float wOcc = WFiltered[id.xy];
    const float wVis = 1.0f - wOcc;

    // Per-pixel rotation to decorrelate stepping.
    const uint h = (id.x * 1664525u + 1013904223u) ^ (id.y * 22695477u);
    const float rot = float(h & 0xFFFFu) * (6.2831853f / 65536.0f);
    float sR, cR;
    sincos(rot, sR, cR);
    const float2 sinCosRot = float2(cR, sR);

    // Single-pass blend: for each tap, accumulate (wVis * Color + wOcc * Filled).
    // wOcc is uniform over the disk, so when it's effectively zero (the central
    // pixel has no occluded neighbours — i.e. flat-lit area away from a
    // silhouette) we skip ColorFilled reads entirely. On iGPU this halves the
    // texture-fetch cost of the dispatch in the common case.
    const uint kTaps = max((uint)DofTapsF, 4u);
    const bool needFilled = wOcc > 0.001f;

    float4 accum = 0;
    uint count = 0;

    if (needFilled)
    {
        [loop]
        for (uint i = 0; i < kTaps; ++i)
        {
            const float2 tap = SunflowerSample(i, kTaps, sinCosRot) * radius;
            const int2 coord = int2(id.xy) + int2(round(tap));
            if (coord.x < 0 || coord.y < 0 ||
                coord.x >= (int)Resolution.x || coord.y >= (int)Resolution.y) continue;

            accum += wVis * ColorTex[coord] + wOcc * ColorFilled[coord];
            count++;
        }
    }
    else
    {
        // Fast path: pure visible-color disk average.
        [loop]
        for (uint i = 0; i < kTaps; ++i)
        {
            const float2 tap = SunflowerSample(i, kTaps, sinCosRot) * radius;
            const int2 coord = int2(id.xy) + int2(round(tap));
            if (coord.x < 0 || coord.y < 0 ||
                coord.x >= (int)Resolution.x || coord.y >= (int)Resolution.y) continue;

            accum += ColorTex[coord];
            count++;
        }
    }

    float4 outLin = (count > 0) ? (accum / (float)count) : ColorTex[id.xy];
    OutDOF[id.xy] = float4(LinearToSrgb(saturate(outLin.rgb)), outLin.a);
}
