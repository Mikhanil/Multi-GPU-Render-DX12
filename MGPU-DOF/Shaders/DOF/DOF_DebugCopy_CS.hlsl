#define THREADS_X 8
#define THREADS_Y 8

cbuffer DOFParamsA : register(b0)
{
    float2 Resolution;
    float FocusDistance;
    float FocusRange;
}

cbuffer DebugParams : register(b2)
{
    uint DebugMode;
}

Texture2D<float> WMax : register(t4);
Texture2D<float> WFiltered : register(t5);

RWTexture2D<float4> OutDOF : register(u5);

[numthreads(THREADS_X, THREADS_Y, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)Resolution.x || id.y >= (uint)Resolution.y) return;

    float basis = (DebugMode == 2) ? WMax[id.xy] : WFiltered[id.xy];
    OutDOF[id.xy] = float4(basis, basis, basis, 1.0f);
}
