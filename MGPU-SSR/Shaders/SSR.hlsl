#include "Common.hlsl"

Texture2D SceneColor : register(t0, space2);
Texture2D SceneDepth : register(t1, space2);
Texture2D SceneNormal : register(t2, space2);

struct VertexIn
{
    float3 PosL : POSITION;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;

    vout.PosH = float4(vin.PosL, 1.0f);
    vout.TexC = vin.TexC;

    return vout;
}

float3 ReconstructViewPosition(float2 uv, float depth)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 viewPos = mul(float4(ndc, depth, 1.0f), worldBuffer.InvProj);
    return viewPos.xyz / viewPos.w;
}

float2 ProjectViewPositionToUv(float3 viewPos)
{
    float4 clipPos = mul(float4(viewPos, 1.0f), worldBuffer.Proj);
    clipPos.xyz /= clipPos.w;
    return float2(clipPos.x * 0.5f + 0.5f, -clipPos.y * 0.5f + 0.5f);
}

bool IsInsideScreen(float2 uv)
{
    return all(uv >= 0.0f) && all(uv <= 1.0f);
}

float EdgeFade(float2 uv)
{
    float2 edge = min(uv, 1.0f - uv);
    return saturate(min(edge.x, edge.y) * worldBuffer.SsrResolveSettings.z);
}

float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

bool SampleSceneDepth(float2 uv, out float sceneDepth, out float3 scenePosV)
{
    sceneDepth = SceneDepth.SampleLevel(gsamPointClamp, uv, 0.0f).r;
    if (sceneDepth >= 1.0f)
    {
        scenePosV = float3(0.0f, 0.0f, 0.0f);
        return false;
    }

    scenePosV = ReconstructViewPosition(uv, sceneDepth);
    return true;
}

bool FindSsrHit(float3 originV, float3 rayDirV, float rayJitter, out float2 hitUv, out float hitDistance)
{
    float maxDistance = max(worldBuffer.SsrRaySettings.x, 0.001f);
    float thickness = max(worldBuffer.SsrRaySettings.y, 0.001f);
    float stride = max(worldBuffer.SsrRaySettings.z, 0.001f);
    uint maxSteps = (uint)max(worldBuffer.SsrResolveSettings.x, 1.0f);
    uint binarySteps = (uint)max(worldBuffer.SsrResolveSettings.y, 0.0f);

    float previousDistance = stride * rayJitter;

    [loop]
    for (uint stepIndex = 1; stepIndex <= maxSteps; ++stepIndex)
    {
        float distance = stride * (stepIndex + rayJitter);
        if (distance > maxDistance)
        {
            break;
        }

        float3 rayPosV = originV + rayDirV * distance;
        if (rayPosV.z <= worldBuffer.NearZ || rayPosV.z >= worldBuffer.FarZ)
        {
            break;
        }

        float2 uv = ProjectViewPositionToUv(rayPosV);
        if (!IsInsideScreen(uv))
        {
            break;
        }

        float sceneDepth;
        float3 scenePosV;
        if (!SampleSceneDepth(uv, sceneDepth, scenePosV))
        {
            previousDistance = distance;
            continue;
        }

        float depthDelta = rayPosV.z - scenePosV.z;
        if (depthDelta >= 0.0f && depthDelta <= thickness)
        {
            float low = previousDistance;
            float high = distance;

            [loop]
            for (uint binaryIndex = 0; binaryIndex < binarySteps; ++binaryIndex)
            {
                float mid = (low + high) * 0.5f;
                float3 midPosV = originV + rayDirV * mid;
                float2 midUv = ProjectViewPositionToUv(midPosV);

                float midDepth;
                float3 midScenePosV;
                if (!IsInsideScreen(midUv) || !SampleSceneDepth(midUv, midDepth, midScenePosV))
                {
                    low = mid;
                    continue;
                }

                if (midPosV.z - midScenePosV.z >= 0.0f)
                {
                    high = mid;
                }
                else
                {
                    low = mid;
                }
            }

            hitDistance = high;
            hitUv = ProjectViewPositionToUv(originV + rayDirV * hitDistance);
            return IsInsideScreen(hitUv);
        }

        previousDistance = distance;
    }

    hitUv = 0.0f;
    hitDistance = 0.0f;
    return false;
}

#ifndef SSR_SKIP_DEFAULT_PS
float4 PS(VertexOut pin) : SV_Target
{
    float4 baseColor = SceneColor.Sample(gsamLinearClamp, pin.TexC);

    float depth = SceneDepth.SampleLevel(gsamPointClamp, pin.TexC, 0.0f).r;
    if (depth >= 1.0f)
    {
        return baseColor;
    }

    float3 normalSample = SceneNormal.SampleLevel(gsamPointClamp, pin.TexC, 0.0f).xyz;
    if (dot(normalSample, normalSample) < 0.01f)
    {
        return baseColor;
    }

    float3 normalV = normalize(normalSample);
    float3 viewPos = ReconstructViewPosition(pin.TexC, depth);
    float3 viewDir = normalize(viewPos);
    float nDotV = saturate(dot(normalV, -viewDir));
    float3 reflectionDir = normalize(reflect(viewDir, normalV));

    if (reflectionDir.z <= 0.0f)
    {
        return baseColor;
    }

    float2 hitUv;
    float hitDistance;
    uint colorWidth;
    uint colorHeight;
    SceneColor.GetDimensions(colorWidth, colorHeight);
    float rayJitter = InterleavedGradientNoise(pin.TexC * float2(colorWidth, colorHeight));

    if (!FindSsrHit(viewPos + normalV * 0.02f, reflectionDir, rayJitter, hitUv, hitDistance))
    {
        return baseColor;
    }

    float3 hitNormalSample = SceneNormal.SampleLevel(gsamPointClamp, hitUv, 0.0f).xyz;
    if (dot(hitNormalSample, hitNormalSample) < 0.01f)
    {
        return baseColor;
    }

    float3 hitNormalV = normalize(hitNormalSample);
    if (dot(reflectionDir, hitNormalV) > 0.2f)
    {
        return baseColor;
    }

    float maxDistance = max(worldBuffer.SsrRaySettings.x, 0.001f);
    float distanceFade = saturate(1.0f - hitDistance / maxDistance);
    float fresnel = 0.04f + 0.96f * pow(1.0f - nDotV, 5.0f);
    float visibility = saturate(0.15f + 0.85f * fresnel);
    float reflectionWeight = saturate(visibility * worldBuffer.SsrRaySettings.w * distanceFade * EdgeFade(hitUv));

    float3 reflectionColor = SceneColor.Sample(gsamLinearClamp, hitUv).rgb;
    return float4(lerp(baseColor.rgb, reflectionColor, reflectionWeight), baseColor.a);
}
#endif
