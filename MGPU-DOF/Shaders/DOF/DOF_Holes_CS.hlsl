#define THREADS_X 8
#define THREADS_Y 8

// Depth-band layer classification + 8-neighbour occlusion test.
// DepthTex is pre-linearised by DOF_DepthQuantize_CS (R16_UNORM = linearZ/FarZ),
// so we just multiply by FarZ to recover view-space metres.

cbuffer DOFParamsA : register(b0)
{
    float2 Resolution;
    float FocusDistance;   // metres
    float FocusRange;      // metres (half-width of in-focus zone)
    float NearZ;           // unused
    float FarZ;            // multiplier to recover metres from R16_UNORM
}

Texture2D<float> DepthTex : register(t0);
RWTexture2D<uint> OcclusionMask : register(u1);

static const int2 kOffsets[8] = {
    int2(-1, -1), int2(0, -1), int2(1, -1),
    int2(-1, 0), int2(1, 0),
    int2(-1, 1), int2(0, 1), int2(1, 1)
};

uint LayerOf(float linearZ, float nearPlane, float farPlane)
{
    // 0 = foreground (near), 1 = in-focus, 2 = background (far)
    if (linearZ < nearPlane) return 0;
    if (linearZ > farPlane)  return 2;
    return 1;
}

[numthreads(THREADS_X, THREADS_Y, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)Resolution.x || id.y >= (uint)Resolution.y) return;

    const float linearZ = DepthTex[id.xy] * FarZ;
    const float nearPlane = FocusDistance - FocusRange;
    const float farPlane  = FocusDistance + FocusRange;

    const uint layer = LayerOf(linearZ, nearPlane, farPlane);

    // Foreground is never blocked.
    if (layer == 0)
    {
        OcclusionMask[id.xy] = 0;
        return;
    }

    // Pixel p is blocked if any 8-neighbour lies in a closer layer.
    uint occluded = 0;
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        int2 coord = int2(id.xy) + kOffsets[i];
        if (coord.x < 0 || coord.y < 0 ||
            coord.x >= (int)Resolution.x || coord.y >= (int)Resolution.y) continue;

        const float zN = DepthTex[coord] * FarZ;
        const uint layerN = LayerOf(zN, nearPlane, farPlane);

        if (layerN < layer)
        {
            occluded = 1;
            break;
        }
    }

    OcclusionMask[id.xy] = occluded;
}
