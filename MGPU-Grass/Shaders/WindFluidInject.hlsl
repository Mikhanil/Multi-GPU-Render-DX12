#include "WindFluid_Common.hlsli"

SamplerState FluidSampler : register(s0);

Texture2D<float2> PrevVelocity : register(t0);
RWTexture2D<float2> OutVelocity : register(u0);

[numthreads(8, 8, 1)]
void CS_Main(uint3 id : SV_DispatchThreadID)
{
    WF_BindGridFromRG(PrevVelocity);

    uint2 ij = id.xy;
    if (ij.x >= WF_GridW || ij.y >= WF_GridH)
        return;

    float2 suv = (float2(ij) + 0.5f) * float2(WF_InvGridW, WF_InvGridH);
    int2 pix = int2((int)ij.x, (int)ij.y);

    if (WindFluid_SolidAtCell(pix) > 0.5f)
    {
        OutVelocity[pix] = float2(0.0f, 0.0f);
        return;
    }

    // Shadertoy Buffer A (SHADERTOY.txt): backtrace advection + left-edge forcing.
    float2 oldVelocity = PrevVelocity.Load(int3((int)ij.x, (int)ij.y, 0)).xy;
    float2 samplePos = WindFluid_BacktraceUv(suv, oldVelocity, Dt);
    float2 vin = PrevVelocity.SampleLevel(FluidSampler, samplePos, 0).xy;

    int2 maxCell = int2((int)WF_GridW, (int)WF_GridH) - int2(1, 1);
    int2 c_l = WindFluid_ClampCell(pix + int2(-1, 0), maxCell);
    int2 c_r = WindFluid_ClampCell(pix + int2(1, 0), maxCell);
    int2 c_up = WindFluid_ClampCell(WindFluid_CellUp(pix), maxCell);
    int2 c_down = WindFluid_ClampCell(WindFluid_CellDown(pix), maxCell);
    float2 vSmooth = (
        PrevVelocity.Load(int3(c_l.x, c_l.y, 0)).xy +
        PrevVelocity.Load(int3(c_r.x, c_r.y, 0)).xy +
        PrevVelocity.Load(int3(c_up.x, c_up.y, 0)).xy +
        PrevVelocity.Load(int3(c_down.x, c_down.y, 0)).xy) * 0.25f;
    vin = lerp(vin, vSmooth, 0.10f);

    // Shadertoy uses a constant leftward force; do not remap from analytic wind origins
    // (those belong in the grass blend path only and cause intersecting diagonal flow here).
    const float2 force = float2(100.0f, 0.0f);

    // Soft inlet (Shadertoy band x<0.06, y in 0.2..0.8) — smooth edges avoid hard shear lines.
    const float inletX = 1.0f - smoothstep(0.0f, 0.06f, suv.x);
    const float inletY = smoothstep(0.18f, 0.24f, suv.y) * (1.0f - smoothstep(0.76f, 0.82f, suv.y));
    const float inletMask = inletX * inletY;
    const float injectScale = max(InjectStrength, 0.0f);
    vin += force * Dt * injectScale * inletMask;

    // Gentle decay each step (UI "Dissipation"); keeps wake from ringing and self-intersecting.
    vin *= Dissipation;

    // LMB click injects a local radial burst into the GPU field (scene pick -> UV).
    if (ClickImpulseStrength > 0.0f)
    {
        float2 toClick = suv - float2(ClickImpulseU, ClickImpulseV);
        toClick.x *= WF_InvGridH / max(WF_InvGridW, 1e-6f);
        if (dot(toClick, toClick) < ClickImpulseRadiusSq)
        {
            const float dLen = length(toClick);
            float2 radial = (dLen > 1e-5f) ? (toClick / dLen) : float2(1.0f, 0.0f);
            vin += radial * ClickImpulseStrength * Dt;
        }
    }

    if (WindFluid_IsBorderUv(suv))
        vin = float2(0.0f, 0.0f);

    OutVelocity[pix] = vin;
}
