#define THREADS_X 8
#define THREADS_Y 8

// Circle-of-Confusion (Zhang 2019 Eq. 1, simplified):
//   CoC(p) = saturate(|linearZ(p) - FocusDistance| / FocusRange) * MaxCoC
// FocusDistance and FocusRange are in view-space units (metres). DepthTex is
// pre-linearised by DOF_DepthQuantize_CS (stored as R16_UNORM = linearZ/FarZ),
// so we just multiply by FarZ to recover metres — no perspective unproject.

cbuffer DOFParamsA : register(b0)
{
    float2 Resolution;
    float FocusDistance;   // metres (view-space Z)
    float FocusRange;      // metres (half-width of in-focus zone)
    float NearZ;           // unused here (depth already linearised on prime)
    float FarZ;            // multiplier to recover metres from [0,1] R16_UNORM
}

cbuffer DOFParamsB : register(b1)
{
    float MaxCoC;
    float FilterRadius; // unused here but kept aligned with root signature
}

Texture2D<float> DepthTex : register(t0);
RWTexture2D<float> OutCoC : register(u0);

[numthreads(THREADS_X, THREADS_Y, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)Resolution.x || id.y >= (uint)Resolution.y) return;

    const float depthSample = DepthTex[id.xy];   // already in [0,1] linear / FarZ
    const float linearZ = depthSample * FarZ;

    const float delta = abs(linearZ - FocusDistance);
    const float coc = saturate(delta / max(FocusRange, 1e-4f)) * MaxCoC;
    OutCoC[id.xy] = coc;
}
