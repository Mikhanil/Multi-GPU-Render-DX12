// ComputeGrass.hlsl
// Compute шейдер для генерации травы на GPU

struct GrassData
{
    float3 Position;
    float Scale;
    float Rotation;
    float WindOffset;
    uint TextureIndex;
    uint3 Padding3;
};

cbuffer GrassEmitterData : register(b0)
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

cbuffer GrassCullData : register(b1)
{
    float4x4 World;
    float4x4 ViewProj;
    float3 EyePos;
    float PadEye0;
    float MaxDistance;
    float Lod0Distance;
    float Lod1Distance;
    uint Lod0BaseSegments;
    float WindTessellationScale;
    float Padding0;
}

RWStructuredBuffer<GrassData> GrassBuffer : register(u0);
StructuredBuffer<GrassData> GrassInput : register(t0);
#ifdef COMPUTE_GRASS_EXPAND_PASS
Texture2D<float2> WindVelocityField : register(t1);
SamplerState WindFluidSampler : register(s0);
#endif

struct GrassRenderVertex
{
    float3 Position;
    float Padding0;
    float2 TexCoord;
    float2 ExtraData;      // x = useTexture(0/1/2), y = bladeHeight01
    float WindStress01;
    float ExtraPad0;
};

RWStructuredBuffer<GrassRenderVertex> ExpandedGrassBuffer : register(u0);
RWStructuredBuffer<uint> VisibleVertexCounter : register(u1);

float Rand(float2 uv)
{
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}

float RandRange(float2 uv, float minVal, float maxVal)
{
    return lerp(minVal, maxVal, Rand(uv));
}

float2 WindRadialDirectionXZ(float2 offsXZ, float radius)
{
    const float dn = length(offsXZ);
    const float eps = max(0.06f * max(radius, 1.0f), 0.5f);
    if (dn <= eps)
    {
        if (dot(offsXZ, offsXZ) < 1e-8f)
            return float2(1.0f, 0.0f);
        return offsXZ / max(dn, 1e-4f);
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
        float mag = min(8.0f, lenA * 2.85f);
        return dir * mag;
    }
    return float2(0.0f, 0.0f);
}

float2 SampleNearestWindVector(float3 worldPos, float2 fallbackDir)
{
    return SampleWindGradient(worldPos, fallbackDir);
}

#ifdef COMPUTE_GRASS_EXPAND_PASS
float2 WorldPosToFluidUv(float3 worldPos)
{
    float he = max(WindFieldWorldParams.z, 1e-4f);
    return float2(
        (worldPos.x - WindFieldWorldParams.x) / (2.0f * he) + 0.5f,
        (worldPos.z - WindFieldWorldParams.y) / (2.0f * he) + 0.5f);
}

// Normalized 0..1 over the grass patch in emitter local XZ (matches grass grid extent).
float2 GrassLocalToNormalizedUv(float3 grassLocalPos)
{
    float halfGrass = max(WorldSize * 0.5f, 1e-4f);
    return float2(
        saturate((grassLocalPos.x + halfGrass) / (2.0f * halfGrass)),
        saturate((grassLocalPos.z + halfGrass) / (2.0f * halfGrass)));
}

// Sim texture UV 0..1 over the grass emitter patch (local XZ). Matches obstacle U/V sliders.
float2 GrassLocalToFluidUv(float3 grassLocalPos)
{
    return GrassLocalToNormalizedUv(grassLocalPos);
}

float2 SampleFluidWindTexelBilinear(float2 suv)
{
    uint gridW = 0u;
    uint gridH = 0u;
    WindVelocityField.GetDimensions(gridW, gridH);
    gridW = max(gridW, 1u);
    gridH = max(gridH, 1u);

    float2 uv = saturate(suv);
    float2 g = uv * float2((float)gridW, (float)gridH) - 0.5f;
    int2 c0 = int2(floor(g));
    float2 f = frac(g);
    int2 maxCell = int2((int)gridW, (int)gridH) - int2(1, 1);
    c0 = clamp(c0, int2(0, 0), maxCell);
    int2 c1 = min(c0 + int2(1, 0), maxCell);
    int2 c2 = min(c0 + int2(0, 1), maxCell);
    int2 c3 = min(c0 + int2(1, 1), maxCell);

    float2 v00 = WindVelocityField.Load(int3(c0.x, c0.y, 0)).xy;
    float2 v10 = WindVelocityField.Load(int3(c1.x, c1.y, 0)).xy;
    float2 v01 = WindVelocityField.Load(int3(c2.x, c2.y, 0)).xy;
    float2 v11 = WindVelocityField.Load(int3(c3.x, c3.y, 0)).xy;
    return lerp(lerp(v00, v10, f.x), lerp(v01, v11, f.x), f.y);
}

float2 SampleFluidWindXZAtUv(float2 uv)
{
    float2 simVel = SampleFluidWindTexelBilinear(saturate(uv));
    float cellWorld = max(WindFieldWorldParams.w, 1e-4f);
    return simVel * cellWorld * 2.75f;
}

float2 SampleFluidWindXZAtGrassLocal(float3 grassLocalPos)
{
    return SampleFluidWindXZAtUv(GrassLocalToFluidUv(grassLocalPos));
}

float2 SampleFluidWindXZ(float3 worldPos)
{
    return SampleFluidWindXZAtUv(WorldPosToFluidUv(worldPos));
}

// Obstacle wake / shadow lean in grass-patch UV (matches fluid sim + ImGui obstacle U/V).
// B.y = obstacle lean strength (0..200 UI). Scales both under-circle and downstream wake.
float Lod0ObstacleWakeLean01(float2 grassUv)
{
    if (WindFluidObstacleA.x < 0.5f || WindFluidObstacleB.w >= 0.5f)
        return 0.0f;

    const float2 uv = saturate(grassUv);
    const float2 center = WindFluidObstacleA.yz;
    const float radius = max(WindFluidObstacleB.x, 0.02f);
    const float wakeAmp = max(WindFluidObstacleB.y, 0.0f);
    const float wakeStrength01 = saturate(wakeAmp * (1.0f / 200.0f));
    if (wakeStrength01 <= 1e-4f)
        return 0.0f;

    float2 rel = uv - center;
    const float distSq = dot(rel, rel);
    const float rSq = radius * radius;

    // Under / inside obstacle shadow (scaled by obstacle lean slider).
    const float underRadius = rSq * 1.45f;
    float underObstacle = 0.0f;
    if (distSq < underRadius)
    {
        const float t = 1.0f - sqrt(distSq / max(underRadius, 1e-6f));
        underObstacle = t * t * wakeStrength01;
    }

    // Localized downstream wake (+U flow in sim). Fades out behind the obstacle.
    const float behind = uv.x - (center.x + radius * 0.55f);
    const float downstream = (behind > 0.0f)
        ? saturate(1.0f - behind / max(radius * 3.5f, 1e-3f))
        : 0.0f;
    const float lateral = exp(-distSq / max(rSq * 2.8f, 1e-6f));
    const float wakeCone = downstream * lateral;
    const float wakeConeBoost = wakeCone * wakeStrength01;

    return saturate(max(wakeConeBoost, underObstacle));
}

// Debug: linear |v| ramp across grass patch (WindFluidObstacleB.w = enable).
// B.x = min |v|, B.y = max |v|, B.z = axis (0 = local X, 1 = local Z). Flow follows WindDirection.
float2 Lod0DebugGradientWindFromLocal(float3 grassLocalPos, out float gradientT)
{
    float2 uv = GrassLocalToNormalizedUv(grassLocalPos);
    gradientT = (WindFluidObstacleB.z < 0.5f) ? uv.x : uv.y;
    float mag = lerp(WindFluidObstacleB.x, WindFluidObstacleB.y, gradientT);
    float2 flowDir = WindDirection;
    if (dot(flowDir, flowDir) < 1e-8f)
        flowDir = float2(1.0f, 0.0f);
    flowDir = normalize(flowDir);
    return flowDir * max(mag, 0.0f);
}

// LOD0: sample GPU fluid velocity at grass-patch UV (same domain as sim + obstacle sliders).
float2 Lod0WindMapOnly(float3 grassLocalPos, out float gradientT)
{
    gradientT = GrassLocalToNormalizedUv(grassLocalPos).x;
    if (WindFluidObstacleB.w >= 0.5f)
        return Lod0DebugGradientWindFromLocal(grassLocalPos, gradientT);

    if (WindFluidEnable < 0.5f)
        return float2(0.0f, 0.0f);

    return SampleFluidWindXZAtGrassLocal(grassLocalPos);
}

float2 VelWorldToLocalXZ(float2 worldVelXZ)
{
    float3 v = float3(worldVelXZ.x, 0.0f, worldVelXZ.y);
    float3 colX = float3(World._11, World._21, World._31);
    float3 colZ = float3(World._13, World._23, World._33);
    return float2(
        dot(v, colX) / max(dot(colX, colX), 1e-6f),
        dot(v, colZ) / max(dot(colZ, colZ), 1e-6f));
}

// Map fluid |v| to 0..1 bend drive.
float Lod0MapSpeed01(float2 worldFluidVel)
{
    if (WindFluidObstacleB.w >= 0.5f)
        return saturate(length(worldFluidVel) * 0.005f);

    const float gustMag = length(worldFluidVel);
    const float leanGain = max(Lod0LeanGain, 0.5f);
    const float lod0Gust = clamp(gustMag * leanGain * 1.45f, 0.0f, 6.0f);
    return saturate(lod0Gust / 6.0f);
}

float2 Lod0BendDir(float2 worldFluidVel)
{
    float2 localVel = VelWorldToLocalXZ(worldFluidVel);
    const float lenL = length(localVel);
    return (lenL > 1e-6f) ? (localVel / lenL) : float2(0.0f, 0.0f);
}

float2 Lod0DefaultFlowBendDir()
{
    float2 flowDir = WindDirection;
    if (dot(flowDir, flowDir) < 1e-8f)
        flowDir = float2(1.0f, 0.0f);
    return Lod0BendDir(normalize(flowDir) * 1.0f);
}

float2 Lod0ResolveBendDir(float2 worldFluidVel, float leanDrive01)
{
    float2 bendDir = Lod0BendDir(worldFluidVel);
    if (dot(bendDir, bendDir) > 1e-8f)
        return bendDir;

    if (leanDrive01 > 1e-4f)
        return Lod0DefaultFlowBendDir();

    return float2(0.0f, 0.0f);
}

float2 BlendFieldWindAtWorld(float3 worldPos, float2 analyticWind)
{
    float2 fieldWindVec = analyticWind;
    if (WindFluidEnable >= 0.5f)
    {
        float2 fluid = SampleFluidWindXZ(worldPos);
        float bf = saturate(WindFluidBlend);
        fieldWindVec = lerp(analyticWind, fluid, bf);
        if (WindFluidPad0 > 0.5f)
            fieldWindVec += analyticWind;
    }
    return fieldWindVec;
}

float2 FieldWindToBendDir(float2 fieldWindVec, float2 baseDir)
{
    const float wLen = length(fieldWindVec);
    float2 flowDir = (wLen > 1e-6f) ? (fieldWindVec / wLen) : baseDir;
    float2 windDirWorld = (wLen > 1e-5f) ? flowDir : baseDir;
    float2 local = VelWorldToLocalXZ(windDirWorld * max(wLen, 1e-6f));
    float l = length(local);
    return (l > 1e-6f) ? (local / l) : baseDir;
}

float ComputeGustMag(float2 fieldWindVec)
{
    return clamp(length(fieldWindVec) * FieldInfluenceScale * 2.0f, 0.0f, 6.0f);
}
#endif

[numthreads(64, 1, 1)]
void CS(uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex, uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint grassIndex = dispatchThreadID.x;
    
    if (grassIndex >= GrassCount)
        return;
    
    uint x = grassIndex % GridSize;
    uint z = grassIndex / GridSize;
    
    float2 randomSeed = float2(grassIndex * 0.123, grassIndex * 0.456);
    
    GrassData grass;
    
    if (z >= GridSize)
    {
        // Для оставшихся элементов - случайные позиции
        grass.Position.x = RandRange(randomSeed, -WorldSize * 0.5f, WorldSize * 0.5f);
        grass.Position.y = 0.0f;
        grass.Position.z = RandRange(randomSeed + 1.0f, -WorldSize * 0.5f, WorldSize * 0.5f);
    }
    else
    {
        // Равномерное распределение по сетке
        float cellSize = WorldSize / float(GridSize);
        float halfWorld = WorldSize * 0.5f;
        
        float baseX = (float(x) + 0.5f) * cellSize - halfWorld;
        float baseZ = (float(z) + 0.5f) * cellSize - halfWorld;
        
        grass.Position.x = baseX + RandRange(randomSeed, -cellSize * 0.4f, cellSize * 0.4f);
        grass.Position.y = 0.0f;
        grass.Position.z = baseZ + RandRange(randomSeed + 1.0f, -cellSize * 0.4f, cellSize * 0.4f);
    }
    
    grass.Scale = RandRange(randomSeed + 2.0f, 0.8f, 1.5f);
    grass.Rotation = RandRange(randomSeed + 3.0f, 0.0f, 6.28318f);
    grass.WindOffset = RandRange(randomSeed + 4.0f, 0.0f, 6.28318f);
    grass.TextureIndex = 0;
    grass.Padding3 = uint3(0, 0, 0);
    
    GrassBuffer[grassIndex] = grass;
}

#ifdef COMPUTE_GRASS_EXPAND_PASS
[numthreads(64, 1, 1)]
void CS_ExpandGrassToVertices(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint grassIndex = dispatchThreadID.x;
    if (grassIndex >= GrassCount)
        return;

    GrassData grass = GrassInput[grassIndex];

    float baseWidth = QuadSize * grass.Scale * 0.5f;
    float baseHeight = QuadSize * grass.Scale;
    float cosR = cos(grass.Rotation);
    float sinR = sin(grass.Rotation);

    float4 worldCenter = mul(float4(grass.Position, 1.0f), World);
    float3 toEye = worldCenter.xyz - EyePos;
    if (dot(toEye, toEye) > (MaxDistance * MaxDistance))
    {
        return;
    }

    // Conservative frustum culling in clip/NDC space.
    float4 clipPos = mul(worldCenter, ViewProj);
    if (clipPos.w <= 1e-5f)
    {
        return;
    }

    float3 ndc = clipPos.xyz / clipPos.w;
    const float frustumMargin = 0.35f;
    if (ndc.x < -1.0f - frustumMargin || ndc.x > 1.0f + frustumMargin ||
        ndc.y < -1.0f - frustumMargin || ndc.y > 1.0f + frustumMargin ||
        ndc.z < -0.25f || ndc.z > 1.25f)
    {
        return;
    }

    float distToEye = length(toEye);
    bool useLod0 = distToEye <= Lod0Distance;
    bool useLod1 = !useLod0 && distToEye <= Lod1Distance;
    bool useLod2 = !useLod0 && !useLod1;

    // Wind: LOD0 = GPU map only; LOD1+ = analytic + optional fluid blend.
    float2 analyticFieldWind = SampleWindGradient(worldCenter.xyz, WindDirection);
#ifdef COMPUTE_GRASS_EXPAND_PASS
    float bladeGradientT = 0.0f;
    float2 fieldWindVec = useLod0
        ? Lod0WindMapOnly(grass.Position, bladeGradientT)
        : BlendFieldWindAtWorld(worldCenter.xyz, analyticFieldWind);
#else
    float2 fieldWindVec = analyticFieldWind;
#endif
    const bool fluidWindActive = WindFluidEnable >= 0.5f && WindFluidBlend > 0.01f;
#ifdef COMPUTE_GRASS_EXPAND_PASS
    const float gustMag = useLod0
        ? (Lod0MapSpeed01(fieldWindVec) * 6.0f)
        : ComputeGustMag(fieldWindVec);
#else
    const float gustMag = clamp(length(fieldWindVec) * FieldInfluenceScale * 2.0f, 0.0f, 6.0f);
#endif
    const float windSpeed01 = saturate(gustMag / 6.0f);
    float2 baseDir = WindDirection;
    baseDir = (dot(baseDir, baseDir) > 1e-8f) ? normalize(baseDir) : float2(1.0f, 0.0f);
#ifdef COMPUTE_GRASS_EXPAND_PASS
    float2 windDir = useLod0 ? Lod0ResolveBendDir(fieldWindVec, windSpeed01) : FieldWindToBendDir(fieldWindVec, baseDir);
#else
    const float wLen = length(fieldWindVec);
    float2 flowDir = (wLen > 1e-6f) ? (fieldWindVec / wLen) : baseDir;
    float2 windDirWorld = (wLen > 1e-5f) ? flowDir : baseDir;
    float2 windDir = windDirWorld;
#endif
    const float fieldMix = fluidWindActive
        ? saturate(0.55f + windSpeed01 * 0.45f)
        : saturate(windSpeed01 * 0.65f + 0.20f);

    // LOD1/LOD2 only: procedural sway driven by intensity/amplitude/strength.
    float wind = 0.0f;
    if (!useLod0)
    {
        float forceOmega = max(0.01f, 2.0f * WindIntensity);
        wind = sin(Time * forceOmega + grass.WindOffset) * gustMag;
        wind += sin(Time * forceOmega * 1.37f + grass.WindOffset) * WindStrength * 0.15f;
    }
    float lodWidthScale = useLod0 ? Lod0BladeWidthScale : Lod1BladeWidthScale;
    float lodHeightScale = useLod0 ? Lod0BladeHeightScale : Lod1BladeHeightScale;
    float width = baseWidth * max(lodWidthScale, 0.05f);
    float height = baseHeight * max(lodHeightScale, 0.05f);
    uint segments = 1u;
    if (useLod0)
    {
        // Tessellation follows wind-field strength only (not intensity/amplitude).
        uint extraSeg = (uint)clamp(floor(windSpeed01 * WindTessellationScale * 2.0f), 0.0f, 2.0f);
        segments = clamp(Lod0BaseSegments + extraSeg, 2u, 6u);
    }

    uint bladeCount = useLod0 ? clamp(Lod0BladeCount, 1u, 4u) : clamp(Lod1BladeCount, 1u, 4u);
    if (useLod2)
    {
        bladeCount = 1u;
        segments = 1u;
    }
    uint vertexCount = bladeCount * segments * 6u;
    uint baseVertex;
    InterlockedAdd(VisibleVertexCounter[0], vertexCount, baseVertex);
    GrassRenderVertex v = (GrassRenderVertex)0;

#ifdef COMPUTE_GRASS_EXPAND_PASS
    float2 bladeBendDir = float2(0.0f, 0.0f);
    if (useLod0)
        bladeBendDir = Lod0ResolveBendDir(fieldWindVec, windSpeed01);
#endif

    [loop]
    for (uint blade = 0u; blade < bladeCount; ++blade)
    {
        float bladeN = (bladeCount > 1u) ? (float(blade) / float(bladeCount - 1u)) : 0.5f;
        float bladeCenter = lerp(-0.75f, 0.75f, bladeN) * width;
        float bladeWidth = width * (useLod0 ? 0.30f : 0.35f);
        float bladePhaseOffset = (bladeN - 0.5f) * 0.6f;

        [loop]
        for (uint seg = 0u; seg < segments; ++seg)
        {
            float t0 = float(seg) / float(segments);
            float t1 = float(seg + 1u) / float(segments);

            float bend0 = t0 * t0;
            float bend1 = t1 * t1;

            float taper0 = useLod0 ? lerp(1.0f, 0.12f, t0) : 1.0f;
            float taper1 = useLod0 ? lerp(1.0f, 0.12f, t1) : 1.0f;
            float w0 = bladeWidth * taper0;
            float w1 = bladeWidth * taper1;

            float x0 = bladeCenter;
            float x1 = bladeCenter;

            float y0 = lerp(0.0f, height, t0);
            float y1 = lerp(0.0f, height, t1);

            // Per-segment field sample so LOD0 bend follows spatial wind map (not just base gust).
            float segGustMag = gustMag;
            float2 segWindDir = windDir;
            float2 segMapVel = float2(0.0f, 0.0f);
            float segSpeed01 = windSpeed01;
            float segGradientT = 0.0f;
#ifdef COMPUTE_GRASS_EXPAND_PASS
            segGradientT = bladeGradientT;
            {
                float tMid = (t0 + t1) * 0.5f;
                float3 segLocal = float3(grass.Position.x, grass.Position.y + height * tMid, grass.Position.z);
                float3 segWorld = mul(float4(segLocal, 1.0f), World).xyz;
                if (useLod0)
                {
                    segMapVel = Lod0WindMapOnly(segLocal, segGradientT);
                    segSpeed01 = Lod0MapSpeed01(segMapVel);
                    if (WindFluidObstacleB.w >= 0.5f)
                        segSpeed01 = max(segSpeed01, saturate(segGradientT));
                    else
                    {
                        const float wakeLean01 = Lod0ObstacleWakeLean01(GrassLocalToFluidUv(segLocal));
                        segSpeed01 = max(segSpeed01, wakeLean01 * 0.98f);
                    }
                    segGustMag = segSpeed01 * 6.0f;
                    segWindDir = Lod0ResolveBendDir(segMapVel, segSpeed01);
                }
                else
                {
                    float2 segField = BlendFieldWindAtWorld(segWorld, analyticFieldWind);
                    segGustMag = ComputeGustMag(segField);
                    segWindDir = FieldWindToBendDir(segField, baseDir);
                }
            }
#endif
            const float segWindSpeed01 = saturate(segGustMag / 6.0f);

            float segCosR = cosR;
            float segSinR = sinR;

            float3 bL = float3(x0 - w0, y0, 0.0f);
            float3 bR = float3(x0 + w0, y0, 0.0f);
            float3 tL = float3(x1 - w1, y1, 0.0f);
            float3 tR = float3(x1 + w1, y1, 0.0f);

            float3 rbL;
            rbL.x = bL.x * segCosR - bL.z * segSinR + grass.Position.x;
            rbL.z = bL.x * segSinR + bL.z * segCosR + grass.Position.z;
            rbL.y = bL.y + grass.Position.y;
            float3 rbR;
            rbR.x = bR.x * segCosR - bR.z * segSinR + grass.Position.x;
            rbR.z = bR.x * segSinR + bR.z * segCosR + grass.Position.z;
            rbR.y = bR.y + grass.Position.y;
            float3 rtL;
            rtL.x = tL.x * segCosR - tL.z * segSinR + grass.Position.x;
            rtL.z = tL.x * segSinR + tL.z * segCosR + grass.Position.z;
            rtL.y = tL.y + grass.Position.y;
            float3 rtR;
            rtR.x = tR.x * segCosR - tR.z * segSinR + grass.Position.x;
            rtR.z = tR.x * segSinR + tR.z * segCosR + grass.Position.z;
            rtR.y = tR.y + grass.Position.y;

            float bendWorld0 = 0.0f;
            float bendWorld1 = 0.0f;
            float bendCapScale = 1.0f;
            if (useLod0)
            {
                float leanProfile0 = t0 * t0;
                float leanProfile1 = t1 * t1;
                float2 bendDir = segWindDir;
                float speed01 = segSpeed01;
                const float wakeLean01 = (WindFluidObstacleB.w >= 0.5f)
                    ? 0.0f
                    : Lod0ObstacleWakeLean01(GrassLocalToFluidUv(
                        float3(grass.Position.x, grass.Position.y + height * ((t0 + t1) * 0.5f), grass.Position.z)));
                const float effectiveSpeed01 = max(speed01, wakeLean01 * 0.98f);
                const float leanGain = max(Lod0LeanGain, 0.5f);
                float gustMag = length(segMapVel) * leanGain;
                if (WindFluidObstacleB.w >= 0.5f)
                {
                    const float gradMag = lerp(WindFluidObstacleB.x, WindFluidObstacleB.y, saturate(segGradientT));
                    gustMag = max(gustMag, gradMag * leanGain);
                }
                float leanDrive = gustMag * lerp(1.35f, 2.45f, effectiveSpeed01);
                leanDrive += wakeLean01 * leanGain * 3.5f;
                bendDir = Lod0ResolveBendDir(segMapVel, effectiveSpeed01);
                float horiz0 = leanDrive * width * 2.25f * leanProfile0;
                float horiz1 = leanDrive * width * 2.25f * leanProfile1;
                bendCapScale = lerp(1.15f, 2.45f, effectiveSpeed01);
                float bendCap0 = max(0.0f, height * bendCapScale * t0);
                float bendCap1 = max(0.0f, height * bendCapScale * t1);
                bendWorld0 = min(horiz0, bendCap0);
                bendWorld1 = min(horiz1, bendCap1);

                float2 offset0 = bendDir * bendWorld0;
                float2 offset1 = bendDir * bendWorld1;
                rbL.x += offset0.x;
                rbL.z += offset0.y;
                rbR.x += offset0.x;
                rbR.z += offset0.y;
                rtL.x += offset1.x;
                rtL.z += offset1.y;
                rtR.x += offset1.x;
                rtR.z += offset1.y;

                float groundLay = effectiveSpeed01 * effectiveSpeed01;
                float groundSink0 = height * groundLay * leanProfile0 * lerp(0.75f, 1.35f, effectiveSpeed01);
                float groundSink1 = height * groundLay * leanProfile1 * lerp(0.75f, 1.35f, effectiveSpeed01);
                rbL.y -= abs(bendWorld0) * lerp(0.65f, 1.55f, effectiveSpeed01) * leanProfile0 + groundSink0;
                rbR.y -= abs(bendWorld0) * lerp(0.65f, 1.55f, effectiveSpeed01) * leanProfile0 + groundSink0;
                rtL.y -= abs(bendWorld1) * lerp(0.65f, 1.55f, effectiveSpeed01) * leanProfile1 + groundSink1;
                rtR.y -= abs(bendWorld1) * lerp(0.65f, 1.55f, effectiveSpeed01) * leanProfile1 + groundSink1;
            }
            else
            {
                float windSeg = (wind + bladePhaseOffset) * (useLod2 ? 0.35f : 1.0f);
                float ripple = sin(Time + grass.WindOffset + bladePhaseOffset) * segGustMag * 0.15f;
                float fieldLean = segGustMag * lerp(0.50f, 1.25f, segWindSpeed01);
                float directionalResponse = windSeg;
                float ampScale = WindAmplitude;
                float bendDrive = fieldLean + abs(directionalResponse) + abs(ripple);
                float bendStrength = lerp(WindStrength, max(segGustMag, 0.55f), saturate(fieldMix + segWindSpeed01 * 0.35f));
                float swayInfluence = lerp(1.0f, 3.0f, segWindSpeed01) + segGustMag * 0.45f;
                bendWorld0 = bendDrive * bendStrength * ampScale * swayInfluence * width * 1.05f * bend0;
                bendWorld1 = bendDrive * bendStrength * ampScale * swayInfluence * width * 1.05f * bend1;
                bendCapScale = lerp(0.50f, 1.55f, segWindSpeed01);
                float bendCap0 = max(0.0f, height * bendCapScale * t0);
                float bendCap1 = max(0.0f, height * bendCapScale * t1);
                bendWorld0 = clamp(bendWorld0, -bendCap0, bendCap0);
                bendWorld1 = clamp(bendWorld1, -bendCap1, bendCap1);
                float2 offset0 = segWindDir * bendWorld0;
                float2 offset1 = segWindDir * bendWorld1;
                rbL.xz += offset0;
                rbR.xz += offset0;
                rtL.xz += offset1;
                rtR.xz += offset1;

                float groundLay = segWindSpeed01 * segWindSpeed01;
                float droopScale = lerp(0.22f, 1.05f, segWindSpeed01);
                float groundSink0 = height * groundLay * t0 * t0 * lerp(0.35f, 0.92f, segWindSpeed01);
                float groundSink1 = height * groundLay * t1 * t1 * lerp(0.35f, 0.92f, segWindSpeed01);
                rbL.y -= abs(bendWorld0) * droopScale * bend0 + groundSink0;
                rbR.y -= abs(bendWorld0) * droopScale * bend0 + groundSink0;
                rtL.y -= abs(bendWorld1) * droopScale * bend1 + groundSink1;
                rtR.y -= abs(bendWorld1) * droopScale * bend1 + groundSink1;
            }

            uint dst = baseVertex + blade * (segments * 6u) + seg * 6u;
            // 0: LOD0 gradient, 1: textured LOD1, 2: textured LOD2 (lower mip in PS).
            float useTexture = useLod0 ? 0.0f : (useLod1 ? 1.0f : 2.0f);
            float bendStress = saturate(abs(bendWorld1) / max(height * bendCapScale, 1e-3f));
            float stress01 = useLod0
                ? ((WindFluidObstacleB.w >= 0.5f)
                    ? saturate(segGradientT * 0.35f + segSpeed01 * 0.65f)
                    : saturate(segSpeed01 * 0.85f + length(segMapVel) * 0.015f + bendStress * 0.35f))
                : saturate(segWindSpeed01 * 0.55f + bendStress * 0.55f);

            v.ExtraData = float2(useTexture, t0);
            v.WindStress01 = stress01 * lerp(0.40f, 1.0f, t0);
            v.Position = rbL; v.TexCoord = float2(0.0f, 1.0f - t0); ExpandedGrassBuffer[dst + 0u] = v;
            v.ExtraData = float2(useTexture, t1);
            v.WindStress01 = stress01 * lerp(0.40f, 1.0f, t1);
            v.Position = rtL; v.TexCoord = float2(0.0f, 1.0f - t1); ExpandedGrassBuffer[dst + 1u] = v;
            v.ExtraData = float2(useTexture, t0);
            v.WindStress01 = stress01 * lerp(0.40f, 1.0f, t0);
            v.Position = rbR; v.TexCoord = float2(1.0f, 1.0f - t0); ExpandedGrassBuffer[dst + 2u] = v;

            v.ExtraData = float2(useTexture, t0);
            v.WindStress01 = stress01 * lerp(0.40f, 1.0f, t0);
            v.Position = rbR; v.TexCoord = float2(1.0f, 1.0f - t0); ExpandedGrassBuffer[dst + 3u] = v;
            v.ExtraData = float2(useTexture, t1);
            v.WindStress01 = stress01 * lerp(0.40f, 1.0f, t1);
            v.Position = rtL; v.TexCoord = float2(0.0f, 1.0f - t1); ExpandedGrassBuffer[dst + 4u] = v;
            v.ExtraData = float2(useTexture, t1);
            v.WindStress01 = stress01 * lerp(0.40f, 1.0f, t1);
            v.Position = rtR; v.TexCoord = float2(1.0f, 1.0f - t1); ExpandedGrassBuffer[dst + 5u] = v;
        }
    }
}
#endif