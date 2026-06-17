#ifndef WIND_FLUID_COMMON_HLSLI
#define WIND_FLUID_COMMON_HLSLI

cbuffer WindFluidConsts : register(b0)
{
    uint GridW;
    uint GridH;
    uint JacobiIterations;
    uint WindOriginCount;
    float InvGridW;
    float InvGridH;
    float Dt;
    float Dissipation;
    float InjectStrength;
    float VorticityEps;
    float WindMapFalloff;
    float CellWorldSize;
    float4 FieldCenterHalf;
    float4 ObstacleWallA;
    float4 ObstacleWallB;
    float4 WindOriginData[4];
    float4 WindDirectionData[4];
    float ClickImpulseU;
    float ClickImpulseV;
    float ClickImpulseStrength;
    float ClickImpulseRadiusSq;
}

// Spatial grid size is taken from the bound simulation texture each dispatch.
// Do not rely on GridW/InvGridW from the CB for UV math — CB size/alignment bugs
// previously made InvGridW 2x too large and zeroed the entire right half (U > 0.5).
static uint WF_GridW;
static uint WF_GridH;
static float WF_InvGridW;
static float WF_InvGridH;

void WF_BindGrid(uint w, uint h)
{
    WF_GridW = max(w, 1u);
    WF_GridH = max(h, 1u);
    WF_InvGridW = 1.0f / float(WF_GridW);
    WF_InvGridH = 1.0f / float(WF_GridH);
}

void WF_BindGridFromRG(Texture2D<float2> tex)
{
    uint w, h;
    tex.GetDimensions(w, h);
    WF_BindGrid(w, h);
}

void WF_BindGridFromR(Texture2D<float> tex)
{
    uint w, h;
    tex.GetDimensions(w, h);
    WF_BindGrid(w, h);
}

float2 SampleAnalyticWind(float3 worldXZW)
{
    uint count = min(WindOriginCount, 4u);
    if (count == 0u)
        return float2(0.f, 0.f);

    float2 accum = 0.f;
    [loop]
    for (uint i = 0u; i < count; ++i)
    {
        float3 origin = WindOriginData[i].xyz;
        float radius = max(1.0f, WindOriginData[i].w);
        float strength = max(0.0f, WindDirectionData[i].w);
        float2 offs = worldXZW.xz - origin.xz;
        float d = length(offs);
        if (strength <= 1e-8f)
            continue;
        float radialMask = saturate(1.0f - d / max(radius, 1e-4f));
        const float2 packedDir = WindDirectionData[i].xy;
        const float dirLen = length(packedDir);
        const float dirBlend = (dirLen > 1e-6f) ? saturate(WindDirectionData[i].z) : 0.0f;
        const float dn = length(offs);
        const float eps = max(0.06f * max(radius, 1.0f), 0.5f);
        float2 radialDir;
        if (dn <= eps)
        {
            radialDir =
                dot(offs, offs) < 1e-8f ? float2(1.f, 0.f) : (offs / max(dn, 1e-4f));
        }
        else
        {
            radialDir = offs / dn;
        }
        float2 directionalDir = (dirLen > 1e-6f) ? (packedDir / dirLen) : radialDir;
        float2 uv = float2(
            (worldXZW.x - FieldCenterHalf.x) / (2.0f * max(FieldCenterHalf.z, 1e-4f)) + 0.5f,
            (worldXZW.z - FieldCenterHalf.y) / (2.0f * max(FieldCenterHalf.z, 1e-4f)) + 0.5f);
        float2 uvC = uv - 0.5f;
        float halfSpan = max(0.05f, 0.5f * (abs(directionalDir.x) + abs(directionalDir.y)));
        float alongN = (dot(uvC, directionalDir) + halfSpan) / max(2.0f * halfSpan, 1e-5f);
        float inletMask = 1.0f - smoothstep(0.02f, 0.08f, alongN);
        float directionalMask = saturate(0.25f + 0.75f * inletMask);
        float mask = lerp(radialMask, directionalMask, dirBlend);
        if (mask <= 1e-6f)
            continue;

        float radialWeight = pow(max(radialMask, 1e-5f), max(0.1f, WindMapFalloff)) * strength;
        float directionalWeight = directionalMask * strength;
        float w = lerp(radialWeight, directionalWeight, dirBlend);

        float2 flowDir = lerp(radialDir, directionalDir, dirBlend);
        flowDir = flowDir / max(length(flowDir), 1e-6f);

        accum += flowDir * w;
    }
    float lenA = length(accum);
    if (lenA > 1e-8f)
    {
        float2 nd = normalize(accum);
        float mag = min(8.0f, lenA * 2.85f);
        return nd * mag;
    }
    return float2(0.f, 0.f);
}

void WindFluid_WallFrame(float2 uv, out float2 rel, out float2 tangent, out float2 normal,
                         out float halfLen, out float halfWidth)
{
    float angle = ObstacleWallA.w;
    tangent = float2(cos(angle), sin(angle));
    normal = float2(-tangent.y, tangent.x);

    rel = uv - ObstacleWallA.yz;
    halfLen = max(0.01f, ObstacleWallB.x);
    halfWidth = max(0.0015f, ObstacleWallB.y);
}

float WindFluid_WallSolidMask(float2 uv)
{
    if (ObstacleWallA.x <= 0.5f)
        return 0.0f;

    // Shadertoy circular barrier (Buffer A): aspect-corrected distance test.
    float2 toBarrier = ObstacleWallA.yz - uv;
    toBarrier.x *= WF_InvGridH / max(WF_InvGridW, 1e-6f);
    const float radiusSq = max(ObstacleWallB.x * ObstacleWallB.x, 1e-6f);
    return dot(toBarrier, toBarrier) < radiusSq ? 1.0f : 0.0f;
}

bool WindFluid_IsBorderCell(int2 cell)
{
    return cell.x <= 0 || cell.y <= 0 ||
           cell.x >= int(WF_GridW) - 1 || cell.y >= int(WF_GridH) - 1;
}

bool WindFluid_IsPressureBorderCell(int2 cell)
{
    return cell.x <= 1 || cell.y <= 1 ||
           cell.x >= int(WF_GridW) - 2 || cell.y >= int(WF_GridH) - 2;
}

bool WindFluid_IsBorderUv(float2 uv)
{
    float2 invRes = float2(WF_InvGridW, WF_InvGridH);
    return (uv.x <= invRes.x || uv.y <= invRes.y ||
            uv.x >= (1.0f - invRes.x) || uv.y >= (1.0f - invRes.y));
}

bool WindFluid_IsPressureBorderUv(float2 uv)
{
    float2 border = float2(WF_InvGridW, WF_InvGridH) * 2.0f;
    return (uv.x < border.x || uv.y < border.y ||
            uv.x > (1.0f - border.x) || uv.y > (1.0f - border.y));
}

float WindFluid_SolidAtCell(int2 cell)
{
    float2 uv = (float2(cell) + 0.5f) * float2(WF_InvGridW, WF_InvGridH);
    float border = WindFluid_IsBorderCell(cell) ? 1.0f : 0.0f;
    return max(border, WindFluid_WallSolidMask(uv));
}

float2 WindFluid_LoadVelWithSolid(Texture2D<float2> vel, int2 cell)
{
    if (WindFluid_SolidAtCell(cell) > 0.5f)
        return float2(0.0f, 0.0f);
    return vel.Load(int3(cell.x, cell.y, 0)).xy;
}

int2 WindFluid_ClampCell(int2 cell, int2 maxCell)
{
    return clamp(cell, int2(0, 0), maxCell);
}

// Shadertoy/OpenGL UV has +v upward; D3D texel +y points downward on screen.
int2 WindFluid_CellUp(int2 cell)
{
    return cell + int2(0, -1);
}

int2 WindFluid_CellDown(int2 cell)
{
    return cell + int2(0, 1);
}

// Buffer A backtrace in D3D storage (Shadertoy: uv - vel * dt * invRes in GL UV).
float2 WindFluid_BacktraceUv(float2 suv, float2 velocity, float dt)
{
    float2 invRes = float2(WF_InvGridW, WF_InvGridH);
    return float2(
        suv.x - velocity.x * dt * invRes.x,
        suv.y + velocity.y * dt * invRes.y);
}

float ScalarCurlAt(int2 p, Texture2D<float2> vin)
{
    int2 maxCell = int2((int)WF_GridW, (int)WF_GridH) - int2(1, 1);

    int2 c_xp = WindFluid_ClampCell(p + int2(1, 0), maxCell);
    int2 c_xm = WindFluid_ClampCell(p + int2(-1, 0), maxCell);
    int2 c_zp = WindFluid_ClampCell(p + int2(0, 1), maxCell);
    int2 c_zm = WindFluid_ClampCell(p + int2(0, -1), maxCell);

    float2 vxp = WindFluid_LoadVelWithSolid(vin, c_xp);
    float2 vxm = WindFluid_LoadVelWithSolid(vin, c_xm);
    float2 vzp = WindFluid_LoadVelWithSolid(vin, c_zp);
    float2 vzm = WindFluid_LoadVelWithSolid(vin, c_zm);

    float dvz_dx = (vxp.y - vxm.y) * 0.5f * WF_InvGridW;
    float dvx_dz = (vzp.x - vzm.x) * 0.5f * WF_InvGridH;
    return dvz_dx - dvx_dz;
}

float3 VorticityGradient(int2 p, Texture2D<float2> vin)
{
    int2 maxCell = int2((int)WF_GridW, (int)WF_GridH) - int2(1, 1);

    float w0 = ScalarCurlAt(p, vin);
    float wxp = ScalarCurlAt(WindFluid_ClampCell(p + int2(1, 0), maxCell), vin);
    float wxm = ScalarCurlAt(WindFluid_ClampCell(p + int2(-1, 0), maxCell), vin);
    float wyp = ScalarCurlAt(WindFluid_ClampCell(p + int2(0, 1), maxCell), vin);
    float wym = ScalarCurlAt(WindFluid_ClampCell(p + int2(0, -1), maxCell), vin);

    float2 gOmega = float2(wxp - wxm, wyp - wym) * 0.25f;
    float len = length(gOmega) + 1e-5f;
    return float3(gOmega.x / len, gOmega.y / len, abs(w0));
}

#endif
