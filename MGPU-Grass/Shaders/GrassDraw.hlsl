// GrassDraw.hlsl
// Шейдер для отрисовки травы с одной текстурой

// Debug switch:
// 1 -> force all grass to a fixed world-space quad (diagnostic mode)
// 0 -> normal grass rendering path
#define GRASS_DEBUG_FIXED_WORLD 0

cbuffer ObjectConstants : register(b0)
{
    float4x4 World;
    float4x4 TextureTransform;
}

cbuffer WorldConstants : register(b1)
{
    float4x4 View;
    float4x4 InvView;
    float4x4 Proj;
    float4x4 InvProj;
    float4x4 ViewProj;
    float4x4 InvViewProj;
    float4x4 ViewProjTex;
    float4x4 ShadowTransform;
    float3 EyePosW;
    float debugMap;
    float2 RenderTargetSize;
    float2 InvRenderTargetSize;
    float NearZ;
    float FarZ;
    float TotalTime;
    float DeltaTime;
    float4 AmbientLight;
    float3 CameraForwardVector;
    float padding;
}

cbuffer GrassEmitterData : register(b2)
{
    uint GrassCount;
    uint GridSize;
    float WorldSize;
    float QuadSize;
    float Time;
    float WindStrength;
    float WindIntensity;
    float WindAmplitude;
    float Lod0BladeWidthScale;
    float Lod0BladeHeightScale;
    float Lod1BladeWidthScale;
    float Lod1BladeHeightScale;
    float Lod0SdofNaturalFreq;
    float Lod0SdofDampingRatio;
    float Lod0DistanceThreshold;
    float Lod1DistanceThreshold;
    uint AtlasTextureCount;
    uint GpuStressIterations;
    uint Lod0BladeCount;
    uint Lod1BladeCount;
    float2 WindDirection;
    uint WindOriginCount;
    float WindMapFalloff;
    float FieldInfluenceScale;
    float DebugNearestOriginTint;
    float WindFluidEnable;
    float WindFluidBlend;
    float WindFluidPad0;
    float Lod0LeanGain;
    float4 WindFieldWorldParams;
    float4 WindOriginData[4];
    float4 WindDirectionData[4];
    float4 WindFluidObstacleA;
    float4 WindFluidObstacleB;
}

float2 WindRadialDirectionXZ(float2 offsXZ, float radius)
{
    const float dn = length(offsXZ);
    const float eps = max(0.06f * max(radius, 1.0f), 0.5f);
    if (dn <= eps)
    {
        // Near gust center avoid unstable normalize(0): use smoothed XY from fractional offset first.
        if (dot(offsXZ, offsXZ) < 1e-8f)
            return float2(1.0f, 0.0f);
        return offsXZ / max(dn, 1e-4f); // radial cap already small
    }
    return offsXZ * rcp(dn);
}

float2 SampleWindGradient(float3 worldPos, float2 fallbackDir)
{
    uint count = min(WindOriginCount, 4u);
    if (count == 0u)
        return float2(0.0f, 0.0f);

    float2 accum = float2(0.0f, 0.0f);
    [loop]
    for (uint i = 0u; i < count; ++i)
    {
        float3 origin = WindOriginData[i].xyz;
        float radius = max(1.0f, WindOriginData[i].w);
        float strength = max(0.0f, WindDirectionData[i].w);
        float2 offs = worldPos.xz - origin.xz;
        float d = length(offs);
        if (strength <= 1e-8f)
            continue;
        // Falloff reaches 0 exactly at boundary; small hub uses stable direction above (no gigantic radius needed).
        float radialMask = saturate(1.0f - d / max(radius, 1e-4f));
        float2 packedDir = WindDirectionData[i].xy;
        float dirLen = length(packedDir);
        float dirBlend = dirLen > 1e-6f ? saturate(WindDirectionData[i].z) : 0.0f;
        float2 radialDir = WindRadialDirectionXZ(offs, radius);
        float2 directionalDir = dirLen > 1e-6f ? (packedDir / dirLen) : radialDir;
        float directionalMask = 1.0f;
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
        float2 dir = accum * rcp(lenA);
        // Keep raw falloff amplitude (was saturate(lenA) ~ always <1 mid-field); gustMag still clamps later on CPU path.
        float mag = min(8.0f, lenA * 2.85f);
        return dir * mag;
    }
    return float2(0.0f, 0.0f);
}

// Kept only for readability; LOD0 previously used nearest-origin vector which quantized badly with non-unit radials.
float2 SampleNearestWindVector(float3 worldPos, float2 fallbackDir)
{
    return SampleWindGradient(worldPos, fallbackDir);
}

uint FindNearestWindOriginIndex(float3 worldPos)
{
    uint count = min(WindOriginCount, 4u);
    if (count == 0u)
        return 0u;
    if (count <= 1u)
        return 0u;

    float bestD = 1e30f;
    uint bestI = 0u;
    [loop]
    for (uint i = 0u; i < count; ++i)
    {
        float d = distance(worldPos.xz, WindOriginData[i].xz);
        if (d < bestD)
        {
            bestD = d;
            bestI = i;
        }
    }
    return bestI;
}

float3 OriginDebugColor(uint idx)
{
    if (idx == 0u) return float3(1.0, 0.2, 0.2); // red
    if (idx == 1u) return float3(0.2, 1.0, 0.2); // green
    if (idx == 2u) return float3(0.2, 0.4, 1.0); // blue
    return float3(1.0, 1.0, 0.2);                // yellow
}

struct GrassData
{
    float3 Position;
    float Scale;
    float Rotation;
    float WindOffset;
    uint TextureIndex;
    uint3 Padding3;
};

struct GrassRenderVertex
{
    float3 Position;
    float Padding0;
    float2 TexCoord;
    float2 ExtraData;      // x = useTexture(0/1/2), y = bladeHeight01
    float WindStress01;
    float ExtraPad0;
};

StructuredBuffer<GrassData> GrassBuffer : register(t0);
StructuredBuffer<GrassRenderVertex> ExpandedGrassVertices : register(t8);
StructuredBuffer<uint> VisibleVertexCounter : register(t9);
Texture2D GrassTexture : register(t1);
SamplerState Sampler : register(s0);

// Вершинный шейдер - читаем данные травы и передаем в геометрический
struct VSInput
{
    uint VertexID : SV_VertexID;
    uint InstanceID : SV_InstanceID;
};

struct VSOutput
{
    float3 WorldPos : POSITION; // blade center in world space
    float Scale : SCALE;
    float Rotation : ROTATION;
    float WindOffset : WINDOFFSET;
    uint TextureIndex : TEX_INDEX;
    uint InstanceID : INSTANCE_ID;
};

VSOutput VS(VSInput input)
{
   // Читаем данные травинки по InstanceID (рендерим все травинки сразу)
    GrassData grass = GrassBuffer[input.InstanceID];
    
    // Применяем мировую трансформацию
    float4 worldPos = mul(float4(grass.Position, 1.0f), World);
    
    VSOutput output = (VSOutput) 0;
    // Keep wind phase and placement in world space so camera motion does not move blades.
    output.WorldPos = worldPos.xyz;
    output.Scale = grass.Scale;
    output.Rotation = grass.Rotation;
    output.WindOffset = grass.WindOffset;
    output.TextureIndex = grass.TextureIndex;
    output.InstanceID = input.InstanceID;
    
    return output;
}

// Геометрический шейдер
struct GSInput
{
    float3 WorldPos : POSITION;
    float Scale : SCALE;
    float Rotation : ROTATION;
    float WindOffset : WINDOFFSET;
    uint TextureIndex : TEX_INDEX;
    uint InstanceID : INSTANCE_ID;
};

struct GSOutput
{
    float4 PositionH : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float3 WorldPos : WORLD_POS;
    float3 Normal : NORMAL;
    float Alpha : ALPHA;
    float UseTexture : TEXCOORD1;
    float BladeHeight01 : TEXCOORD2;
};

GSOutput CreateQuadVertex(GSInput input, float2 offset, float2 uv, float windFactor, float useTexture,
                          float bladeHeight01, float bladeWidthScale, float bladeHeightScale)
{
    GSOutput output;

    float3 worldOrigin = mul(float4(0.0f, 0.0f, 0.0f, 1.0f), World).xyz;
    float3 worldAxisX = mul(float4(1.0f, 0.0f, 0.0f, 1.0f), World).xyz - worldOrigin;
    float3 worldAxisY = mul(float4(0.0f, 1.0f, 0.0f, 1.0f), World).xyz - worldOrigin;
    float objectScaleX = max(length(worldAxisX), 1e-3f);
    float objectScaleY = max(length(worldAxisY), 1e-3f);

    float width = QuadSize * input.Scale * 0.5f * objectScaleX * max(bladeWidthScale, 0.05f);
    float height = QuadSize * input.Scale * objectScaleY * max(bladeHeightScale, 0.05f);

#if GRASS_DEBUG_FIXED_WORLD
    const float3 debugCenterW = float3(0.0f, 0.0f, 0.0f);
    float3 up = float3(0.0f, 1.0f, 0.0f);
    float3 right = float3(1.0f, 0.0f, 0.0f);
    float sideOffset = offset.x * width;
    float verticalOffset = offset.y * height;
    float3 worldPos = debugCenterW + right * sideOffset + up * verticalOffset;
#else
    
    // Build a camera-facing basis in world space, anchored at blade center.
    float3 toEye = EyePosW - input.WorldPos;
    toEye.y = 0.0f;
    float toEyeLen2 = dot(toEye, toEye);
    float3 forward = (toEyeLen2 > 1e-6f) ? normalize(toEye) : float3(0.0f, 0.0f, 1.0f);
    float3 up = float3(0.0f, 1.0f, 0.0f);
    float3 right = normalize(cross(up, forward));

    // Per-blade yaw variation around world up.
    float cosR = cos(input.Rotation);
    float sinR = sin(input.Rotation);
    float3 rotatedRight = right * cosR + forward * sinR;

    float sideOffset = offset.x * width;
    float verticalOffset = saturate(offset.y) * height;
    float bendFactor = saturate(offset.y);
    float bendProfile = bendFactor * bendFactor;
    float2 windVec = SampleWindGradient(input.WorldPos, WindDirection);
    float gustMag = clamp(length(windVec) * FieldInfluenceScale, 0.0f, 6.0f);
    float2 gustDir = (dot(windVec, windVec) > 1e-8f) ? normalize(windVec) : float2(0.0f, 0.0f);
    float2 baseDir = WindDirection;
    baseDir = (dot(baseDir, baseDir) > 1e-8f) ? normalize(baseDir) : float2(1.0f, 0.0f);
    float2 windDir = (gustMag > 5e-3f) ? gustDir : baseDir;
    float windSpeed01 = saturate(gustMag / 6.0f);

    float3 worldWindOffset = float3(0.0f, 0.0f, 0.0f);
    if (useTexture < 0.5f)
    {
        // LOD0: field-only lean toward ground — ignores intensity/amplitude/strength.
        const float leanGain = max(Lod0LeanGain, 0.5f);
        float lod0Gust = clamp(gustMag * leanGain, 0.18f * leanGain, 6.0f);
        float windSpeed01 = saturate(lod0Gust / 6.0f);
        float fieldOsc = sin(Time * 1.35f + input.WindOffset);
        float leanDrive = lod0Gust * lerp(1.15f, 2.05f, windSpeed01);
        leanDrive += abs(fieldOsc) * lod0Gust * 0.22f;
        float directionalBend = leanDrive * width * 1.85f * bendProfile;
        float bendCap = max(0.0f, height * lerp(1.05f, 2.05f, windSpeed01) * bendFactor);
        directionalBend = min(directionalBend, bendCap);
        worldWindOffset = float3(windDir.x, 0.0f, windDir.y) * directionalBend;
        float groundLay = max(windSpeed01 * windSpeed01, 0.08f);
        verticalOffset -= abs(directionalBend) * lerp(0.55f, 1.45f, windSpeed01) * bendProfile;
        verticalOffset -= height * groundLay * bendProfile * lerp(0.70f, 1.25f, windSpeed01);
    }
    else
    {
        float ampScale = WindAmplitude;
        float directionalResponse = windFactor;
        float staticLay = 0.70f * gustMag;
        float procAmbientScale = min(4.2f, WindIntensity * (0.30f + 0.12f * min(WindStrength, 4.0f)));
        procAmbientScale *= saturate(gustMag * 0.45f);
        float swayInfluence = min(6.5f, 0.80f * gustMag + procAmbientScale);
        float dynamicSway = directionalResponse * (0.24f + 0.10f * gustMag);
        float bendDrive = staticLay + dynamicSway;
        bendDrive = max(staticLay * 0.10f, bendDrive);
        float directionalBend = bendDrive * WindStrength * ampScale * swayInfluence * width * 1.15f * bendProfile;
        const float bendCap = max(0.0f, height * 0.55f * bendFactor);
        directionalBend = clamp(directionalBend, -bendCap, bendCap);
        worldWindOffset = float3(windDir.x, 0.0f, windDir.y) * directionalBend;
        verticalOffset -= abs(directionalBend) * 0.26f * bendProfile;
    }

    float3 worldPos = input.WorldPos + rotatedRight * sideOffset + up * verticalOffset + worldWindOffset;
#endif
    
    output.WorldPos = worldPos;
    output.PositionH = mul(float4(worldPos, 1.0f), ViewProj);
    output.TexCoord = uv;
    output.Normal = float3(0, 1, 0);
    output.Alpha = 1.0f;
    output.UseTexture = useTexture;
    output.BladeHeight01 = bladeHeight01;
    
    return output;
}

[maxvertexcount(64)]
void GS(point GSInput input[1], inout TriangleStream<GSOutput> triStream)
{
    GSInput grass = input[0];
    
    // Эффект ветра
    float windPhase = grass.WorldPos.x * 0.5f + grass.WorldPos.z * 0.3f + grass.WindOffset;
    float forceOmega = max(0.01f, 2.0f * WindIntensity);
    float forceSignal = sin(Time * forceOmega + windPhase);
    
    // Single-GPU path: approximate LOD0 by segmenting close blades.
    float distToEye = distance(EyePosW, grass.WorldPos);
    bool useLod0 = distToEye <= Lod0DistanceThreshold;
    bool useLod1 = !useLod0 && distToEye <= Lod1DistanceThreshold;
    bool useLod2 = !useLod0 && !useLod1;
    float windFactor = forceSignal;
    if (useLod0)
    {
        // LOD0 uses static field lean in CreateQuadVertex; no SDOF/intensity sway here.
        windFactor = 0.0f;
    }
    float bladeWidthScale = useLod0 ? Lod0BladeWidthScale : Lod1BladeWidthScale;
    float bladeHeightScale = useLod0 ? Lod0BladeHeightScale : Lod1BladeHeightScale;
    if (useLod0)
    {
        // Near-field: render several thin separated strips per instance
        // so the silhouette reads as individual blades rather than one card.
        const uint bladeCount = clamp(Lod0BladeCount, 1u, 4u);
        const uint segments = 4u;
        const float useTexture = 0.0f;
        const float bladeHalfWidth = 0.04f;
        const float bladeSpread = 0.75f;

        [loop]
        for (uint blade = 0u; blade < bladeCount; ++blade)
        {
            float bladeN = (bladeCount > 1u) ? (float(blade) / float(bladeCount - 1u)) : 0.5f;
            float bladeCenter = lerp(-bladeSpread, bladeSpread, bladeN);
            float bladePhaseOffset = (bladeN - 0.5f) * 0.6f;

            [loop]
            for (uint seg = 0u; seg < segments; ++seg)
            {
                float t0 = float(seg) / float(segments);
                float t1 = float(seg + 1u) / float(segments);
                float y0 = t0;
                float y1 = t1;

                float taper0 = lerp(1.0f, 0.12f, t0);
                float taper1 = lerp(1.0f, 0.12f, t1);
                float xL0 = bladeCenter - bladeHalfWidth * taper0;
                float xR0 = bladeCenter + bladeHalfWidth * taper0;
                float xL1 = bladeCenter - bladeHalfWidth * taper1;
                float xR1 = bladeCenter + bladeHalfWidth * taper1;
                float windBlade = windFactor + bladePhaseOffset;

                triStream.Append(CreateQuadVertex(grass, float2(xL0, y0), float2(0.0f, 1.0f - t0), windBlade, useTexture, t0, bladeWidthScale, bladeHeightScale));
                triStream.Append(CreateQuadVertex(grass, float2(xL1, y1), float2(0.0f, 1.0f - t1), windBlade, useTexture, t1, bladeWidthScale, bladeHeightScale));
                triStream.Append(CreateQuadVertex(grass, float2(xR0, y0), float2(1.0f, 1.0f - t0), windBlade, useTexture, t0, bladeWidthScale, bladeHeightScale));
                triStream.Append(CreateQuadVertex(grass, float2(xR1, y1), float2(1.0f, 1.0f - t1), windBlade, useTexture, t1, bladeWidthScale, bladeHeightScale));
                triStream.RestartStrip();
            }
        }
    }
    else if (useLod1)
    {
        const uint bladeCount = clamp(Lod1BladeCount, 1u, 4u);
        const float useTexture = 1.0f;
        const float bladeHalfWidth = 0.16f;
        const float bladeSpread = 0.9f;
        [loop]
        for (uint blade = 0u; blade < bladeCount; ++blade)
        {
            float bladeN = (bladeCount > 1u) ? (float(blade) / float(bladeCount - 1u)) : 0.5f;
            float bladeCenter = lerp(-bladeSpread, bladeSpread, bladeN);
            float xL = bladeCenter - bladeHalfWidth;
            float xR = bladeCenter + bladeHalfWidth;

            triStream.Append(CreateQuadVertex(grass, float2(xL, 0.0f), float2(0.0f, 1.0f), windFactor, useTexture, 0.0f, bladeWidthScale, bladeHeightScale));
            triStream.Append(CreateQuadVertex(grass, float2(xL, 1.0f), float2(0.0f, 0.0f), windFactor, useTexture, 1.0f, bladeWidthScale, bladeHeightScale));
            triStream.Append(CreateQuadVertex(grass, float2(xR, 0.0f), float2(1.0f, 1.0f), windFactor, useTexture, 0.0f, bladeWidthScale, bladeHeightScale));
            triStream.Append(CreateQuadVertex(grass, float2(xR, 1.0f), float2(1.0f, 0.0f), windFactor, useTexture, 1.0f, bladeWidthScale, bladeHeightScale));
            triStream.RestartStrip();
        }
    }
    else
    {
        // LOD2: maximum optimization, no wind, single card.
        const float useTexture = 2.0f; // Mark LOD2 so pixel shader can sample lower mip.
        const float w = 0.10f;
        triStream.Append(CreateQuadVertex(grass, float2(-w, 0.0f), float2(0.0f, 1.0f), 0.0f, useTexture, 0.0f, Lod1BladeWidthScale, Lod1BladeHeightScale));
        triStream.Append(CreateQuadVertex(grass, float2(-w, 1.0f), float2(0.0f, 0.0f), 0.0f, useTexture, 1.0f, Lod1BladeWidthScale, Lod1BladeHeightScale));
        triStream.Append(CreateQuadVertex(grass, float2(w, 0.0f), float2(1.0f, 1.0f), 0.0f, useTexture, 0.0f, Lod1BladeWidthScale, Lod1BladeHeightScale));
        triStream.Append(CreateQuadVertex(grass, float2(w, 1.0f), float2(1.0f, 0.0f), 0.0f, useTexture, 1.0f, Lod1BladeWidthScale, Lod1BladeHeightScale));
        triStream.RestartStrip();
    }
}

// Пиксельный шейдер
struct PSInput
{
    float4 PositionH : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float3 WorldPos : WORLD_POS;
    float3 Normal : NORMAL;
    float Alpha : ALPHA;
    float UseTexture : TEXCOORD1;
    float BladeHeight01 : TEXCOORD2;
};

float4 PS(PSInput input) : SV_Target
{
    float4 color;
    if (input.UseTexture > 0.5f)
    {
        // LOD2 uses an explicit higher mip level for cheaper texture sampling.
        if (input.UseTexture > 1.5f)
            color = GrassTexture.SampleLevel(Sampler, input.TexCoord, 3.0f);
        else
            color = GrassTexture.Sample(Sampler, input.TexCoord);
        clip(color.a - 0.1f);
    }
    else
    {
        float t = saturate(input.BladeHeight01);
        float3 base = float3(0.10f, 0.38f, 0.10f);
        float3 tip = float3(0.32f, 0.78f, 0.20f);
        color = float4(lerp(base, tip, t), 1.0f);
    }
    
    // Простое освещение
    float3 lightDir = normalize(float3(0.5f, -0.5f, 0.5f));
    float diff = max(0.3f, dot(input.Normal, -lightDir));
    
    float3 ambient = float3(0.2f, 0.2f, 0.2f);
    float3 lighting = ambient + diff;
    float3 outRgb = color.rgb * lighting;
    if (DebugNearestOriginTint > 0.5f)
    {
        const uint i = FindNearestWindOriginIndex(input.WorldPos);
        const float3 tint = OriginDebugColor(i);
        outRgb = lerp(outRgb, tint, 0.65f);
    }
    return float4(outRgb, color.a);
}

struct ExpandedVSOut
{
    float4 PositionH : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
    float3 WorldPos : WORLD_POS;
    float3 Normal : NORMAL;
    float Alpha : ALPHA;
    float UseTexture : TEXCOORD1;
    float BladeHeight01 : TEXCOORD2;
    float WindStress01 : TEXCOORD3;
};

ExpandedVSOut VS_Expanded(uint vertexID : SV_VertexID)
{
    ExpandedVSOut o = (ExpandedVSOut)0;
    uint visibleVertexCount = VisibleVertexCounter[0];
    if (vertexID >= visibleVertexCount)
    {
        o.PositionH = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return o;
    }
    GrassRenderVertex v = ExpandedGrassVertices[vertexID];
    float3 pos = v.Position;
    float4 posW = mul(float4(pos, 1.0f), World);
    o.PositionH = mul(posW, ViewProj);
    o.TexCoord = v.TexCoord;
    o.WorldPos = posW.xyz;
    o.Normal = float3(0.0f, 1.0f, 0.0f);
    o.Alpha = 1.0f;
    o.UseTexture = v.ExtraData.x;
    o.BladeHeight01 = v.ExtraData.y;
    o.WindStress01 = v.WindStress01;
    return o;
}

float4 PS_Expanded(ExpandedVSOut input) : SV_Target
{
    clip(input.Alpha - 0.5f);
    float4 color;
    if (input.UseTexture > 0.5f)
    {
        if (input.UseTexture > 1.5f)
            color = GrassTexture.SampleLevel(Sampler, input.TexCoord, 3.0f);
        else
            color = GrassTexture.Sample(Sampler, input.TexCoord);
        clip(color.a - 0.1f);
    }
    else
    {
        float t = saturate(input.BladeHeight01);
        float3 base = float3(0.10f, 0.38f, 0.10f);
        float3 tip = float3(0.32f, 0.78f, 0.20f);
        color = float4(lerp(base, tip, t), 1.0f);
    }
    float3 lightDir = normalize(float3(0.5f, -0.5f, 0.5f));
    float diff = max(0.3f, dot(input.Normal, -lightDir));
    float3 ambient = float3(0.2f, 0.2f, 0.2f);
    float3 lighting = ambient + diff;
    float3 outRgb = color.rgb * lighting;

    float windStress = saturate(input.WindStress01);
    float heightWeight = saturate(input.BladeHeight01);
    float darken = windStress * lerp(0.45f, 1.0f, heightWeight);
    outRgb *= lerp(1.0f, 0.62f, darken);
    outRgb *= lerp(float3(1.0f, 1.0f, 1.0f), float3(0.58f, 0.74f, 0.52f), darken * 0.65f);

    return float4(outRgb, color.a);
}