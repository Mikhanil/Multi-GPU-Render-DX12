#ifndef SKY_COMMON_HLSL
#define SKY_COMMON_HLSL

cbuffer CONSTANT_BUFFER : register(b0)
{
	float4x4 gViewProjMat;

	float4 gColor;

	float3 gSunIlluminance;
	int gScatteringMaxPathDepth;

	float gFrameTimeSec;
	float gTimeSec;
	uint2 gMouseLastDownPos;

	uint gFrameId;
	uint gTerrainResolution;
	float2 RayMarchMinMaxSPP;

    uint2 gGameResolution;
    uint2 gTransmittanceLutResolution;
	
    uint2 gMultiScatLutResolution;
    uint2 gSkyViewLutResolution;
	
    uint3 gAerialPerpspectiveLutResolution;
	float gScreenshotCaptureActive;
	
    uint2 gRayMarchingResolution;
    float2 pad;
	
    float3 terrainPosDelta;
    float pad2;
};

/*
Texture2D<float4>  texture2d							: register(t0);
Texture2D<float4>  BlueNoise2dTexture					: register(t1);

RWTexture2D<float4> rwTexture2d							: register(u0);

SamplerState samplerLinearClamp : register(s0);
SamplerComparisonState  samplerShadow : register(s1);
*/

#endif // SKY_COMMON_HLSL
