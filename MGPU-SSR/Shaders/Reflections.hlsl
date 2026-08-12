#include "Common.hlsl"

static const uint ReflectionProbeCount = 4;
TextureCube ReflectionProbes[ReflectionProbeCount] : register(t0, space3);

struct ReflectionProbeGpuData
{
    float4 Position;
    float4 ProxyBoxMin;
    float4 ProxyBoxMax;
};

struct ReflectionProbeConstants
{
    ReflectionProbeGpuData Probes[ReflectionProbeCount];
};

ConstantBuffer<ReflectionProbeConstants> ReflectionProbe : register(b2);

uint FindClosestReflectionProbe(float3 surfacePositionW)
{
    uint closestProbe = 0;
    float3 toProbe = surfacePositionW - ReflectionProbe.Probes[0].Position.xyz;
    float closestDistanceSquared = dot(toProbe, toProbe);

    [unroll]
    for (uint probeIndex = 1; probeIndex < ReflectionProbeCount; ++probeIndex)
    {
        toProbe = surfacePositionW - ReflectionProbe.Probes[probeIndex].Position.xyz;
        float distanceSquared = dot(toProbe, toProbe);
        if (distanceSquared < closestDistanceSquared)
        {
            closestProbe = probeIndex;
            closestDistanceSquared = distanceSquared;
        }
    }

    return closestProbe;
}

float3 BoxProjectedLookupDirection(float3 surfacePositionW, float3 reflectionDirectionW, uint probeIndex)
{
    // Work in the selected probe's local space so the cubemap direction points
    // from its capture origin to the reflected ray's proxy-box intersection.
    ReflectionProbeGpuData probe = ReflectionProbe.Probes[probeIndex];
    float3 probePosition = probe.Position.xyz;
    float3 surfaceFromProbe = surfacePositionW - probePosition;
    float3 boxMinFromProbe = probe.ProxyBoxMin.xyz - probePosition;
    float3 boxMaxFromProbe = probe.ProxyBoxMax.xyz - probePosition;

    float3 directionSign = lerp(-1.0f, 1.0f, step(0.0f, reflectionDirectionW));
    float3 safeDirection = directionSign * max(abs(reflectionDirectionW), 1.0e-4f);
    float3 targetBounds = lerp(boxMinFromProbe, boxMaxFromProbe, step(0.0f, safeDirection));
    float3 distances = (targetBounds - surfaceFromProbe) / safeDirection;
    // The point is inside the proxy box. Select the nearest *forward* plane;
    // taking the minimum of all three distances also includes negative planes
    // behind the ray and collapses the lookup direction to the surface point.
    float3 forwardDistances = distances;
    if (forwardDistances.x <= 0.0f) forwardDistances.x = 1.0e20f;
    if (forwardDistances.y <= 0.0f) forwardDistances.y = 1.0e20f;
    if (forwardDistances.z <= 0.0f) forwardDistances.z = 1.0e20f;
    float hitDistance = min(forwardDistances.x, min(forwardDistances.y, forwardDistances.z));

    return normalize(surfaceFromProbe + reflectionDirectionW * hitDistance);
}

float3 SampleReflectionProbe(uint probeIndex, float3 lookupDirection)
{
    return ReflectionProbes[NonUniformResourceIndex(probeIndex)]
        .Sample(gsamLinearWrap, lookupDirection).rgb;
}

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION0;
    float3 NormalW : NORMAL;
};

VertexOut REFLECTIONS_VS(VertexIn vin)
{
    VertexOut vout;
    float4 posW = mul(float4(vin.PosL, 1.0f), objectBuffer.World);
    vout.PosW = posW.xyz;
    vout.NormalW = normalize(mul(vin.NormalL, (float3x3)objectBuffer.World));
    vout.PosH = mul(posW, worldBuffer.ViewProj);
    return vout;
}

float4 REFLECTIONS_PS(VertexOut pin) : SV_Target
{
    float3 toEyeW = normalize(worldBuffer.EyePosW - pin.PosW);
    float3 reflectionDirection = reflect(-toEyeW, normalize(pin.NormalW));
    uint probeIndex = FindClosestReflectionProbe(pin.PosW);
    float3 correctedDirection = BoxProjectedLookupDirection(pin.PosW, reflectionDirection, probeIndex);
    float3 reflectionColor = SampleReflectionProbe(probeIndex, correctedDirection);

    // Alpha zero marks probe-reflection pixels so SSR neither shades them nor samples
    // their color as a screen-space hit. The final presentation path does not use alpha.
    return float4(reflectionColor, 0.0f);
}
