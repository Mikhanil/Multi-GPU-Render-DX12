#ifndef XE_GTAO_COMMON_INCLUDED
#define XE_GTAO_COMMON_INCLUDED

#define XE_GTAO_FP32_DEPTHS 1
#define XE_GTAO_USE_HALF_FLOAT_PRECISION 0
#define XE_GTAO_DEFAULT_THIN_OBJECT_HEURISTIC 1
#define XE_GTAO_USE_DEFAULT_CONSTANTS 1
#define VA_SATURATE(x) saturate(x)

#include "XeGTAO.h"
#include "XeGTAO.hlsli"

cbuffer GTAOConstantBuffer : register(b0)
{
    GTAOConstants gtaoConstants;
};

GTAOConstants LoadGTAOConstants()
{    
    return gtaoConstants;
}

#endif // XE_GTAO_COMMON_INCLUDED
