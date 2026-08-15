#include "Common.hlsl"

Texture2D<float4> SceneColor : register(t0, space2);
Texture2D<float> SceneDepth : register(t1, space2);
Texture2D<float4> SceneNormal : register(t2, space2);

struct VertexOut
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

VertexOut VS(uint vertexId : SV_VertexID)
{
    VertexOut output;
    const float2 position = float2((vertexId << 1) & 2, vertexId & 2);
    output.TexCoord = position;
    output.Position = float4(position.x * 2.0f - 1.0f, 1.0f - position.y * 2.0f, 0.0f, 1.0f);
    return output;
}

float ViewDepth(float deviceDepth)
{
    return worldBuffer.Proj[3][2] / (deviceDepth - worldBuffer.Proj[2][2]);
}

float3 ViewPosition(float2 uv, float deviceDepth)
{
    const float z = ViewDepth(deviceDepth);
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    return float3(ndc.x * z / worldBuffer.Proj[0][0],
                  ndc.y * z / worldBuffer.Proj[1][1], z);
}

float2 ProjectToUv(float3 positionV)
{
    const float inverseZ = rcp(positionV.z);
    return float2(positionV.x * worldBuffer.Proj[0][0] * inverseZ * 0.5f + 0.5f,
                  0.5f - positionV.y * worldBuffer.Proj[1][1] * inverseZ * 0.5f);
}

bool InsideScreen(float2 uv)
{
    return uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f;
}

float GradientNoise(float2 pixel)
{
    return frac(52.9829189f * frac(pixel.x * 0.06711056f + pixel.y * 0.00583715f));
}

bool FindHit(float3 originV, float3 directionV, float jitter, out float2 hitUv, out float hitDistance)
{
    const float maxDistance = max(worldBuffer.SsrMaxDistance, 0.001f);
    const float thickness = max(worldBuffer.SsrThickness, 0.001f);
    const float stride = max(worldBuffer.SsrStride, 0.001f);
    const uint maxSteps = max(worldBuffer.SsrMaxSteps, 1u);
    const uint binarySteps = worldBuffer.SsrBinarySteps;
    const float2 originProjected = originV.xy * float2(worldBuffer.Proj[0][0], worldBuffer.Proj[1][1]);
    const float2 directionProjected = directionV.xy * float2(worldBuffer.Proj[0][0], worldBuffer.Proj[1][1]);
    float previousDistance = stride * jitter;

    [loop]
    for (uint stepIndex = 1; stepIndex <= maxSteps; ++stepIndex)
    {
        const float distance = stride * (stepIndex + jitter);
        if (distance > maxDistance)
            break;

        const float rayZ = originV.z + directionV.z * distance;
        if (rayZ <= worldBuffer.NearZ || rayZ >= worldBuffer.FarZ)
            break;

        const float2 rayProjected = originProjected + directionProjected * distance;
        const float inverseRayZ = rcp(rayZ);
        const float2 uv = float2(rayProjected.x * inverseRayZ * 0.5f + 0.5f,
                                 0.5f - rayProjected.y * inverseRayZ * 0.5f);
        if (!InsideScreen(uv))
            break;

        const float sceneDeviceDepth = SceneDepth.SampleLevel(gsamPointClamp, uv, 0.0f);
        if (sceneDeviceDepth >= 1.0f)
        {
            previousDistance = distance;
            continue;
        }

        const float depthDelta = rayZ - ViewDepth(sceneDeviceDepth);
        if (depthDelta >= 0.0f && depthDelta <= thickness)
        {
            float low = previousDistance;
            float high = distance;
            [loop]
            for (uint binaryIndex = 0; binaryIndex < binarySteps; ++binaryIndex)
            {
                const float middle = (low + high) * 0.5f;
                const float middleZ = originV.z + directionV.z * middle;
                const float2 middleProjected = originProjected + directionProjected * middle;
                const float inverseMiddleZ = rcp(middleZ);
                const float2 middleUv = float2(middleProjected.x * inverseMiddleZ * 0.5f + 0.5f,
                                               0.5f - middleProjected.y * inverseMiddleZ * 0.5f);
                if (!InsideScreen(middleUv))
                {
                    low = middle;
                    continue;
                }
                const float middleDepth = SceneDepth.SampleLevel(gsamPointClamp, middleUv, 0.0f);
                const float middleSceneZ = middleDepth < 1.0f ? ViewDepth(middleDepth) : worldBuffer.FarZ;
                if (middleZ >= middleSceneZ)
                    high = middle;
                else
                    low = middle;
            }

            hitDistance = high;
            const float hitZ = originV.z + directionV.z * high;
            const float2 hitProjected = originProjected + directionProjected * high;
            const float inverseHitZ = rcp(hitZ);
            hitUv = float2(hitProjected.x * inverseHitZ * 0.5f + 0.5f,
                           0.5f - hitProjected.y * inverseHitZ * 0.5f);
            return InsideScreen(hitUv);
        }
        previousDistance = distance;
    }

    hitUv = 0.0f;
    hitDistance = 0.0f;
    return false;
}

float4 PS(VertexOut input) : SV_Target
{
    const uint2 pixel = uint2(input.Position.xy);
    const float4 baseColor = SceneColor.Load(int3(pixel, 0));
    const float3 sceneNormal = SceneNormal.Load(int3(pixel, 0)).xyz;

    const float deviceDepth = SceneDepth.Load(int3(pixel, 0));
    if (deviceDepth >= 1.0f)
        return baseColor;

    const float3 normalV = normalize(sceneNormal);
    const float3 positionV = ViewPosition(input.TexCoord, deviceDepth);
    const float3 viewDirection = normalize(positionV);
    const float3 reflectionDirection = reflect(viewDirection, normalV);
    if (reflectionDirection.z <= 0.0f)
        return baseColor;

    float2 hitUv;
    float hitDistance;
    const float jitter = GradientNoise(input.Position.xy);
    if (!FindHit(positionV + normalV * 0.02f, reflectionDirection, jitter, hitUv, hitDistance))
        return baseColor;

    const float3 hitNormal = SceneNormal.SampleLevel(gsamPointClamp, hitUv, 0.0f).xyz;
    if (dot(hitNormal, hitNormal) < 0.01f || dot(reflectionDirection, normalize(hitNormal)) > 0.2f)
        return baseColor;

    const float2 edgeDistance = min(hitUv, 1.0f - hitUv);
    const float edgeFade = saturate(min(edgeDistance.x, edgeDistance.y) * worldBuffer.SsrEdgeFadeScale);
    const float distanceFade = saturate(1.0f - hitDistance / worldBuffer.SsrMaxDistance);
    const float nDotV = saturate(dot(normalV, -viewDirection));
    const float fresnelBase = 1.0f - nDotV;
    const float fresnel2 = fresnelBase * fresnelBase;
    const float fresnel = 0.04f + 0.96f * fresnel2 * fresnel2 * fresnelBase;
    const float weight = saturate((0.15f + 0.85f * fresnel) * worldBuffer.SsrIntensity *
                                  distanceFade * edgeFade);
    const float3 reflectionColor = SceneColor.Sample(gsamLinearClamp, hitUv).rgb;
    return float4(lerp(baseColor.rgb, reflectionColor, weight), baseColor.a);
}
