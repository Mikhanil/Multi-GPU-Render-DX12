#define THREADS_X 8
#define THREADS_Y 8

// One-time linearisation + 16-bit quantisation of the hardware non-linear NDC
// depth buffer. Output is stored as R16_UNORM = (linearZ / FarZ) clamped to
// [0,1]. After this pass:
//   * cross-adapter transfer ships 2 bytes/pixel instead of 4 — half the
//     PCIe bandwidth on the depth path.
//   * downstream shaders (CoC, Holes) just multiply by FarZ to recover view-
//     space metres — no perspective un-projection per pixel.
//   * R16_UNORM gives 65535 evenly-spaced values across [0, FarZ], which is
//     a much better depth distribution for DOF than R32_FLOAT NDC where the
//     vast majority of values bunch up near 1.0.

cbuffer DOFParamsA : register(b0)
{
    float2 Resolution;
    float FocusDistance;   // unused here
    float FocusRange;      // unused here
    float NearZ;
    float FarZ;
}

Texture2D<float>   NdcDepth       : register(t7);  // PrimeDepth (R32_FLOAT view)
RWTexture2D<float> LinearDepthOut : register(u1);  // PrimeDepthQuantized (R16_UNORM)

[numthreads(THREADS_X, THREADS_Y, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)Resolution.x || id.y >= (uint)Resolution.y) return;

    const float ndc = NdcDepth[id.xy];
    // D3D perspective: ndcZ = f*(z-n)/(z*(f-n))  →  z = n*f / (f - ndc*(f-n)).
    const float linearZ = (NearZ * FarZ) / max(FarZ - ndc * (FarZ - NearZ), 1e-6f);
    LinearDepthOut[id.xy] = saturate(linearZ / FarZ);
}
