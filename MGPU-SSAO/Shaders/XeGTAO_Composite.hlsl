#pragma kernel Composite
#pragma multi_compile_local _ XE_GTAO_COMPUTE_BENT_NORMALS

#include "XeGTAOCommon.hlsl"

Texture2D<uint> _GTAOTerm : register(t0);
RWTexture2D<lpfloat> _AOFinal : register(u0);

cbuffer CompositeConstants : register(b1)
{
    float4 _GTAOResolutionScale;
    float _Intensity;
};

float4 R8G8B8A8_UNORM_to_FLOAT4(uint packedInput)
{
    float4 unpackedOutput;
    unpackedOutput.x = (float) (packedInput & 0x000000ff) / 255.0;
    unpackedOutput.y = (float) (((packedInput >> 8) & 0x000000ff)) / 255.0;
    unpackedOutput.z = (float) (((packedInput >> 16) & 0x000000ff)) / 255.0;
    unpackedOutput.w = (float) (packedInput >> 24) / 255.0;
    return unpackedOutput;
}

void DecodeVisibilityBentNormal(const uint packedValue, out float visibility, out float3 bentNormal)
{
    float4 decoded = R8G8B8A8_UNORM_to_FLOAT4(packedValue);
    bentNormal = decoded.xyz * 2.0 - 1.0;
    visibility = decoded.w;
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void Composite(const uint2 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 pixelCoords = dispatchThreadID.xy;
    const uint packedValue = _GTAOTerm.Load(uint3(pixelCoords * (uint2) _GTAOResolutionScale.xy, 0)).r;

    float visibility = 1.0f;
    float3 bentNormalWS = float3(0, 0, 1);
    
#if XE_GTAO_COMPUTE_BENT_NORMALS
    DecodeVisibilityBentNormal(packedValue, visibility, bentNormalWS);
#else
    visibility = packedValue / 255.05;
#endif
    
    visibility = saturate(pow(visibility, _Intensity));
    _AOFinal[pixelCoords] = visibility;
}
