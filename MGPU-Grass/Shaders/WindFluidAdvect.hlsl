#include "WindFluid_Common.hlsli"

Texture2D<float2> VelRead : register(t0);
RWTexture2D<float2> VelWrite : register(u0);

[numthreads(8, 8, 1)]
void CS_Main(uint3 id : SV_DispatchThreadID)
{
    WF_BindGridFromRG(VelRead);

    uint2 ij = id.xy;
    if (ij.x >= WF_GridW || ij.y >= WF_GridH)
        return;

    int2 pix = int2((int)ij.x, (int)ij.y);

    // Inject pass already performs advection and forcing (Shadertoy-style Buffer A).
    float2 v = VelRead.Load(int3(pix.x, pix.y, 0)).xy;
    if (WindFluid_SolidAtCell(pix) > 0.5f || WindFluid_IsBorderCell(pix))
        v = float2(0.0f, 0.0f);

    VelWrite[pix] = v;
}
