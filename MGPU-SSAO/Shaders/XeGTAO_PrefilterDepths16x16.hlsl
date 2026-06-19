#pragma kernel CSPrefilterDepths16x16

#include "XeGTAOCommon.hlsl"

Texture2D<float> g_srcRawDepth : register(t0);
RWTexture2D<lpfloat> g_outWorkingDepthMIP0 : register(u0);
RWTexture2D<lpfloat> g_outWorkingDepthMIP1 : register(u1);
RWTexture2D<lpfloat> g_outWorkingDepthMIP2 : register(u2);
RWTexture2D<lpfloat> g_outWorkingDepthMIP3 : register(u3);
RWTexture2D<lpfloat> g_outWorkingDepthMIP4 : register(u4);

SamplerState sampler_PointClamp : register(s0);

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void CSPrefilterDepths16x16(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID)
{
    const GTAOConstants gtaoConstants = LoadGTAOConstants();
    XeGTAO_PrefilterDepths16x16(dispatchThreadID, groupThreadID, gtaoConstants, g_srcRawDepth, sampler_PointClamp,
                                g_outWorkingDepthMIP0, g_outWorkingDepthMIP1, g_outWorkingDepthMIP2,
                                g_outWorkingDepthMIP3, g_outWorkingDepthMIP4);
}
