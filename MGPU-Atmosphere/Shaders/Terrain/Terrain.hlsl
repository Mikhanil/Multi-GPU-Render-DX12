// Copyright Epic Games, Inc. All Rights Reserved.

// #include "../Common.hlsl"
#include "../SkyAtmosphere/SkyAtmosphereCommon.hlsl"
#include "../SkyAtmosphere/ComputeSkyCommon.hlsl"

Texture2D terrainHeightMap : register(t0, space1);

// const static uint gTerrainResolution_ = 512;
const static uint gTextureResolution = 512;
#ifndef DEPTH_PASS
const static float terrainWidth = 100.0f;
#else
const static float terrainWidth = 100.0f;
#endif
// Texture2D terrainHeightMap : register(t0);

// unused
struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 TangentU : TANGENT;
};

struct VertexOut
{
    float4 PosView : SV_POSITION;
#ifndef DEPTH_PASS
    float3 PosW : POSITION0;
    // float4 SsaoPosH : POSITION1;
    // float4 ShadowPosH : POSITION2;
    float3 NormalW : NORMAL;
    float3 TangentW : TANGENT;
    float2 TexC : TEXCOORD;
#endif
};

// #include "SkyAtmosphereCommon.hlsl"
float4 SampleTerrain(in float quadx, in float quady, in float3 qp)
{
    // const float terrainWidth = 100.0f; // 100 km edge
    const float maxTerrainHeight = 100.0f;
    const float quadWidth = terrainWidth / gTerrainResolution;

    float2 Uvs = (float2(quadx, quady) + qp.xy) / gTerrainResolution;
#if 0
	const float height = terrainHeightMap.SampleLevel(samplerLinearClamp, Uvs, 0).r;
#else
    const float offset = 0.0008;
    float HeightAccum = terrainHeightMap.SampleLevel(samplerLinearClamp, Uvs + float2(0.0f, 0.0f), 0).r;
    HeightAccum += terrainHeightMap.SampleLevel(samplerLinearClamp, Uvs + float2(offset, 0.0f), 0).r;
    HeightAccum += terrainHeightMap.SampleLevel(samplerLinearClamp, Uvs + float2(-offset, 0.0f), 0).r;
    HeightAccum += terrainHeightMap.SampleLevel(samplerLinearClamp, Uvs + float2(0.0f, offset), 0).r;
    HeightAccum += terrainHeightMap.SampleLevel(samplerLinearClamp, Uvs + float2(0.0f, -offset), 0).r;
    const float height = HeightAccum / 5;
#endif
    float4 WorldPos = float4(
	(float3(
			quadx,
			quady,
			maxTerrainHeight * height) + qp)
	* quadWidth - 0.5f * float3(terrainWidth, terrainWidth, 0),
	1.0f);
    // WorldPos.z -= maxTerrainHeight * quadWidth * 0.5f;
    WorldPos.xyz += float3(-terrainWidth * 0.45, 0.4 * terrainWidth, -0.0f); // offset to position view
    return WorldPos;
}


VertexOut TerrainVS(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VertexOut output = (VertexOut) 0;

    uint quadId = instanceId;
    uint vertId = vertexId;

	// For a range on screen in [-0.5,0.5]
    float3 qp = 0.0f;
    qp = vertId == 1 || vertId == 4 ? float3(1.0f, 0.0f, 0.0f) : qp;
    qp = vertId == 2 || vertId == 3 ? float3(0.0f, 1.0f, 0.0f) : qp;
    qp = vertId == 5 ? float3(1.0f, 1.0f, 0.0f) : qp;

    const float TerrainResolutionInv = 1.0 / float(gTerrainResolution);
    const float quadx = quadId / gTerrainResolution;
    const float quady = quadId % gTerrainResolution;

    float4 WorldPos = SampleTerrain(quadx, quady, qp);
    WorldPos.xyz += terrainPosDelta;

    output.PosView = mul(gSkyViewProjMat, WorldPos);
#ifndef DEPTH_PASS
    float2 Uvs = (float2(quadx, quady) + qp.xy) / gTerrainResolution / terrainWidth;
    output.PosW = WorldPos;
    output.TexC = Uvs;

	{
        const float offset = 5.0;
        float3 WorldPos0_ = SampleTerrain(quadx + qp.x - offset, quady + qp.y, 0.0f);
        float3 WorldPos1_ = SampleTerrain(quadx + qp.x + offset, quady + qp.y, 0.0f);
        float3 WorldPos_0 = SampleTerrain(quadx + qp.x, quady + qp.y - offset, 0.0f);
        float3 WorldPos_1 = SampleTerrain(quadx + qp.x, quady + qp.y + offset, 0.0f);
        output.TangentW = normalize(WorldPos1_.xyz - WorldPos0_.xyz);
        output.NormalW = cross(output.TangentW, normalize(WorldPos_1.xyz - WorldPos_0.xyz));
        output.NormalW = normalize(output.NormalW);
    }

    // Generate projective tex-coords to project shadow map onto scene.
    // output.ShadowPosH = mul(WorldPos, worldBuffer.ShadowTransform);
    // output.ShadowPosH.z = 0;
#endif
    return output;
}

float4 TerrainPS(VertexOut input) : SV_TARGET
{
    float NoL = max(0.0, dot(sun_direction, normalize(input.NormalW)));
#if TERRAIN_SHADOW_ENABLED
    float4 shadowUv = mul(gShadowmapViewProjMat, input.PosW);
	//shadowUv /= shadowUv.w;	// not needed as it is an ortho projection
    shadowUv.x = shadowUv.x * 0.5 + 0.5;
    shadowUv.y = -shadowUv.y * 0.5 + 0.5;
    
    float sunShadow = ShadowmapTexture.SampleCmpLevelZero(samplerShadow, shadowUv.xy, shadowUv.z);
#if 1
	// Bad hard coded shadow PCF filter
    float cnt = 1.0f;
    for (float u = -3.0; u <= 3.0f; u += 1.0f)
    {
        for (float v = -3.0; v <= 3.0f; v += 1.0f)
        {
            float offsetx = u * 0.0001;
            float offsety = v * 0.0001;
            sunShadow += ShadowmapTexture.SampleCmpLevelZero(samplerShadow, shadowUv.xy + float2(-offsetx, -offsety), shadowUv.z);
            cnt++;
        }
    }
    sunShadow /= cnt;
#endif
#endif

	// Second evaluate transmittance due to participating media
    AtmosphereParameters Atmosphere = GetAtmosphereParameters();
    float3 P0 = input.PosW + float3(0, 0, Atmosphere.BottomRadius);
    float viewHeight = length(P0);
    const float3 UpVector = P0 / viewHeight;
    float viewZenithCosAngle = dot(sun_direction, UpVector);
    float2 uv;
    LutTransmittanceParamsToUv(Atmosphere, viewHeight, viewZenithCosAngle, uv);
    const float3 trans = TransmittanceLutTexture.SampleLevel(samplerLinearClamp, uv, 0).rgb;

    float3 terrainColor = float(0.05).rrr;
    
#if TERRAIN_SHADOW_ENABLED
    return float4(terrainColor * sunShadow * NoL * trans, 1);
#else
    return float4(terrainColor * NoL * trans, 1);
#endif
}

// Terrain shader code is a shame but it does what it needs to in the end.

/*
Texture2D<float4>  TerrainHeightmapTex				: register(t0);
Texture2D<float4>  ShadowmapTexture					: register(t1);
Texture2D<float4>  TransmittanceLutTexture			: register(t2);

struct TerrainVertexOutput
{
	float4 position		: SV_POSITION;
	float4 Uvs			: TEXCOORD0;
	float4 WorldPos		: TEXCOORD1;
	float3 color		: COLOR;
	float3 normal		: TEXCOORD2;
};

float4 SampleTerrain(in float quadx, in float quady, in float3 qp)
{
	const float terrainWidth = 100.0f;	// 100 km edge
	const float maxTerrainHeight = 100.0f;
	const float quadWidth = terrainWidth / gTerrainResolution;

	float2 Uvs = (float2(quadx, quady) + qp.xy) / gTerrainResolution;
#if 0
	const float height = TerrainHeightmapTex.SampleLevel(samplerLinearClamp, Uvs, 0).r;
#else
	const float offset = 0.0008;
	float HeightAccum = TerrainHeightmapTex.SampleLevel(samplerLinearClamp, Uvs + float2(0.0f, 0.0f), 0).r;
	HeightAccum+= TerrainHeightmapTex.SampleLevel(samplerLinearClamp, Uvs + float2( offset, 0.0f), 0).r;
	HeightAccum+= TerrainHeightmapTex.SampleLevel(samplerLinearClamp, Uvs + float2(-offset, 0.0f), 0).r;
	HeightAccum+= TerrainHeightmapTex.SampleLevel(samplerLinearClamp, Uvs + float2( 0.0f, offset), 0).r;
	HeightAccum+= TerrainHeightmapTex.SampleLevel(samplerLinearClamp, Uvs + float2( 0.0f,-offset), 0).r;
	const float height = HeightAccum / 5;
#endif
	float4 WorldPos = float4((float3(quadx, quady, maxTerrainHeight * height) + qp) * quadWidth - 0.5f * float3(terrainWidth, terrainWidth, 0.0), 1.0f);
	WorldPos.xyz += float3(-terrainWidth * 0.45, 0.4*terrainWidth, -0.0f);	// offset to position view
	return WorldPos;
}

TerrainVertexOutput TerrainVS(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
	TerrainVertexOutput output = (TerrainVertexOutput)0;

	uint quadId = instanceId;
	uint vertId = vertexId;

	// For a range on screen in [-0.5,0.5]
	float3 qp = 0.0f;
	qp = vertId == 1 || vertId == 4 ? float3(1.0f, 0.0f, 0.0f) : qp;
	qp = vertId == 2 || vertId == 3 ? float3(0.0f, 1.0f, 0.0f) : qp;
	qp = vertId == 5 ? float3(1.0f, 1.0f, 0.0f) : qp;

	const float TerrainResolutionInv = 1.0 / float(gTerrainResolution);
	const float quadx = quadId / gTerrainResolution;
	const float quady = quadId % gTerrainResolution;

	float2 Uvs = (float2(quadx, quady) + qp.xy) / gTerrainResolution;	
	float4 WorldPos = SampleTerrain(quadx, quady, qp);


	output.WorldPos = WorldPos;
	output.Uvs.xy = Uvs;
	output.position = mul(gViewProjMat, WorldPos);

	output.color = 0.05 * (1.0 - gScreenshotCaptureActive);

	{
		const float offset = 5.0;
		float4 WorldPos0_ = SampleTerrain(quadx + qp.x - offset, quady + qp.y, 0.0f);
		float4 WorldPos1_ = SampleTerrain(quadx + qp.x + offset, quady + qp.y, 0.0f);
		float4 WorldPos_0 = SampleTerrain(quadx + qp.x,     quady + qp.y - offset, 0.0f);
		float4 WorldPos_1 = SampleTerrain(quadx + qp.x,     quady + qp.y + offset, 0.0f);
		output.normal = cross(normalize(WorldPos1_.xyz - WorldPos0_.xyz), normalize(WorldPos_1.xyz - WorldPos_0.xyz));
		output.normal = normalize(output.normal);
	}

	return output;
}
*/

