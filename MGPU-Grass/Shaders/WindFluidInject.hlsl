#include "WindFluid_Common.hlsli"

SamplerState FluidSampler : register(s0);

Texture2D<float2> PrevVelocity : register(t0);
RWTexture2D<float2> OutVelocity : register(u0);

float2 BaseFlowForceAtUv(float2 uv)
{
    float2 force = float2(0.0f, 0.0f);
    uint count = min(WindOriginCount, 4u);

    [loop]
    for (uint i = 0u; i < count; ++i)
    {
        // DirectionData.z == 1 marks the constant directional base flow.
        // Radial LMB impulses are handled separately below.
        float directional = saturate(WindDirectionData[i].z);
        float strength = max(WindDirectionData[i].w, 0.0f);
        float2 direction = WindDirectionData[i].xy;
        float directionLength = length(direction);
        if (directional <= 1e-4f || strength <= 1e-5f || directionLength <= 1e-5f)
            continue;

        direction /= directionLength;
        float2 perpendicular = float2(-direction.y, direction.x);
        float2 centeredUv = uv - 0.5f;

        // Rotate the inlet band with the requested flow direction.
        float alongExtent = max(0.5f * (abs(direction.x) + abs(direction.y)), 1e-4f);
        float along01 = (dot(centeredUv, direction) + alongExtent) / (2.0f * alongExtent);
        float inletMask = 1.0f - smoothstep(0.0f, 0.08f, along01);

        // Base flow coverage is packed as the source radius by GrassApp.
        float fieldHalf = max(FieldCenterHalf.z, 1e-4f);
        float coverage = saturate(WindOriginData[i].w / (fieldHalf * 2.1f));
        float lateralExtent = max(0.5f * (abs(perpendicular.x) + abs(perpendicular.y)), 1e-4f);
        float coveredHalfWidth = max(lateralExtent * coverage, 0.02f);
        float lateralMask = 1.0f - smoothstep(
            coveredHalfWidth * 0.82f, coveredHalfWidth, abs(dot(centeredUv, perpendicular)));

        force += direction * strength * directional * inletMask * lateralMask;
    }

    return force * 100.0f;
}

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

    // The directional inlet follows GrassApp's base-flow strength, angle and coverage.
    // InjectStrength remains the independent solver-force multiplier.
    const float2 force = BaseFlowForceAtUv(suv);
    const float injectScale = max(InjectStrength, 0.0f);
    vin += force * Dt * injectScale;

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
            // Use the same global forcing scale as the directional source.  The CPU value is
            // already in solver-force units; multiplying it by an additional magic constant
            // made a held click saturate the grass bend in a single frame.
            vin += radial * ClickImpulseStrength * Dt * injectScale;
        }
    }

    if (WindFluid_IsBorderUv(suv))
        vin = float2(0.0f, 0.0f);

    OutVelocity[pix] = vin;
}
