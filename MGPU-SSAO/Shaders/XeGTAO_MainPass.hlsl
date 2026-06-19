#pragma kernel CSGTAOLow
#pragma kernel CSGTAOMedium
#pragma kernel CSGTAOHigh
#pragma kernel CSGTAOUltra

#pragma multi_compile_local _ XE_GTAO_COMPUTE_BENT_NORMALS

#include "XeGTAOCommon.hlsl"

Texture2D<lpfloat> g_srcWorkingDepth : register(t0);
RWTexture2D<uint> g_outWorkingAOTerm : register(u0);
RWTexture2D<unorm float> g_outWorkingEdges : register(u1);

SamplerState sampler_PointClamp : register(s0);
Texture2D<float4> g_srcSceneNormals : register(t1);

#define NoiseIndex 0

lpfloat3 LoadNormal(int2 pos)
{
    float3 n = g_srcSceneNormals.Load(uint3(pos, 0)).xyz;
    return (lpfloat3)normalize(n);
}

lpfloat2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)
{
    uint seed = pixCoord.x + (pixCoord.y << 15);
    return lpfloat2(frac(seed * 0.618034), frac(seed * 0.382966));
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSGTAOLow(const uint2 pixCoord : SV_DispatchThreadID)
{
    const GTAOConstants gtaoConstants = LoadGTAOConstants();
    XeGTAO_MainPass(pixCoord, 1, 2,
                    SpatioTemporalNoise(pixCoord, NoiseIndex), LoadNormal(pixCoord), gtaoConstants,
                    g_srcWorkingDepth, sampler_PointClamp, g_outWorkingAOTerm, g_outWorkingEdges);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSGTAOMedium(const uint2 pixCoord : SV_DispatchThreadID)
{
    const GTAOConstants gtaoConstants = LoadGTAOConstants();
    XeGTAO_MainPass(pixCoord, 2, 2,
                    SpatioTemporalNoise(pixCoord, NoiseIndex), LoadNormal(pixCoord), gtaoConstants,
                    g_srcWorkingDepth, sampler_PointClamp, g_outWorkingAOTerm, g_outWorkingEdges);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSGTAOHigh(const uint2 pixCoord : SV_DispatchThreadID)
{
    const GTAOConstants gtaoConstants = LoadGTAOConstants();
    XeGTAO_MainPass(pixCoord, 3, 3,
                    SpatioTemporalNoise(pixCoord, NoiseIndex), LoadNormal(pixCoord), gtaoConstants,
                    g_srcWorkingDepth, sampler_PointClamp, g_outWorkingAOTerm, g_outWorkingEdges);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSGTAOUltra(const uint2 pixCoord : SV_DispatchThreadID)
{
    const GTAOConstants gtaoConstants = LoadGTAOConstants();
    XeGTAO_MainPass(pixCoord, 9, 3,
                    SpatioTemporalNoise(pixCoord, NoiseIndex), LoadNormal(pixCoord), gtaoConstants,
                    g_srcWorkingDepth, sampler_PointClamp, g_outWorkingAOTerm, g_outWorkingEdges);
}
