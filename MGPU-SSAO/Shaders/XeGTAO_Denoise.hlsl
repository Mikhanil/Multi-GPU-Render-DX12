#pragma kernel CSDenoisePass
#pragma kernel CSDenoiseLastPass

#pragma multi_compile_local _ XE_GTAO_COMPUTE_BENT_NORMALS

#include "XeGTAOCommon.hlsl"

Texture2D<uint> g_srcWorkingAOTerm : register(t0);
Texture2D<lpfloat> g_srcWorkingEdges : register(t1);
RWTexture2D<uint> g_outFinalAOTerm : register(u0);

SamplerState sampler_PointClamp : register(s0);

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSDenoisePass(const uint2 dispatchThreadID : SV_DispatchThreadID)
{
    const GTAOConstants gtaoConstants = LoadGTAOConstants();
    const uint2 pixCoordBase = dispatchThreadID * uint2(2, 1);
    XeGTAO_Denoise(pixCoordBase, gtaoConstants, g_srcWorkingAOTerm, g_srcWorkingEdges, sampler_PointClamp, g_outFinalAOTerm, false);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSDenoiseLastPass(const uint2 dispatchThreadID : SV_DispatchThreadID)
{
    const GTAOConstants gtaoConstants = LoadGTAOConstants();
    const uint2 pixCoordBase = dispatchThreadID * uint2(2, 1);
    XeGTAO_Denoise(pixCoordBase, gtaoConstants, g_srcWorkingAOTerm, g_srcWorkingEdges, sampler_PointClamp, g_outFinalAOTerm, true);
}
