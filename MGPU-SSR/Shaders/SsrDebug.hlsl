#define SSR_SKIP_DEFAULT_PS 1
#include "SSR.hlsl"

float4 DebugSsrHitStateColor(uint state)
{
    if (state == 0u)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    if (state == 1u)
    {
        return float4(0.0f, 0.0f, 1.0f, 1.0f);
    }
    if (state == 2u)
    {
        return float4(1.0f, 1.0f, 0.0f, 1.0f);
    }
    if (state == 3u)
    {
        return float4(1.0f, 0.0f, 0.0f, 1.0f);
    }
    if (state == 4u)
    {
        return float4(1.0f, 0.0f, 1.0f, 1.0f);
    }

    return float4(0.0f, 1.0f, 0.0f, 1.0f);
}

float4 PS(VertexOut pin) : SV_Target
{
    float depth = SceneDepth.SampleLevel(gsamPointClamp, pin.TexC, 0.0f).r;
    if (depth >= 1.0f)
    {
        return DebugSsrHitStateColor(0u);
    }

    float3 normalSample = SceneNormal.SampleLevel(gsamPointClamp, pin.TexC, 0.0f).xyz;
    if (dot(normalSample, normalSample) < 0.01f)
    {
        return DebugSsrHitStateColor(1u);
    }

    float3 normalV = normalize(normalSample);
    float3 viewPos = ReconstructViewPosition(pin.TexC, depth);
    float3 viewDir = normalize(viewPos);
    float3 reflectionDir = normalize(reflect(viewDir, normalV));

    if (reflectionDir.z <= 0.0f)
    {
        return DebugSsrHitStateColor(2u);
    }

    float2 hitUv;
    float hitDistance;
    uint colorWidth;
    uint colorHeight;
    SceneColor.GetDimensions(colorWidth, colorHeight);
    float rayJitter = InterleavedGradientNoise(pin.TexC * float2(colorWidth, colorHeight));

    if (!FindSsrHit(viewPos + normalV * 0.02f, reflectionDir, rayJitter, hitUv, hitDistance))
    {
        return DebugSsrHitStateColor(3u);
    }

    float3 hitNormalSample = SceneNormal.SampleLevel(gsamPointClamp, hitUv, 0.0f).xyz;
    if (dot(hitNormalSample, hitNormalSample) < 0.01f)
    {
        return DebugSsrHitStateColor(1u);
    }

    float3 hitNormalV = normalize(hitNormalSample);
    if (dot(reflectionDir, hitNormalV) > 0.2f)
    {
        return DebugSsrHitStateColor(4u);
    }

    return DebugSsrHitStateColor(5u);
}
