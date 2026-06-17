#include "WindFluid_Common.hlsli"

Texture2D<float2> VelIn : register(t0);
Texture2D<float> Pin : register(t1);
RWTexture2D<float2> VelOut : register(u0);

[numthreads(8, 8, 1)]
void CS_Main(uint3 id : SV_DispatchThreadID)
{
    WF_BindGridFromRG(VelIn);

    uint2 ij = id.xy;
    if (ij.x >= WF_GridW || ij.y >= WF_GridH)
        return;

    int2 maxCell = int2((int)WF_GridW, (int)WF_GridH) - int2(1, 1);

    int2 pix = int2((int)ij.x, (int)ij.y);
    if (WindFluid_SolidAtCell(pix) > 0.5f)
    {
        VelOut[pix] = float2(0.0f, 0.0f);
        return;
    }
    float2 v = VelIn.Load(int3((int)ij.x, (int)ij.y, 0)).xy;

    int2 c_l = WindFluid_ClampCell(pix + int2(-1, 0), maxCell);
    int2 c_r = WindFluid_ClampCell(pix + int2(1, 0), maxCell);
    int2 c_up = WindFluid_ClampCell(WindFluid_CellUp(pix), maxCell);
    int2 c_down = WindFluid_ClampCell(WindFluid_CellDown(pix), maxCell);

    float pl = Pin.Load(int3(c_l.x, c_l.y, 0)).x;
    float pr = Pin.Load(int3(c_r.x, c_r.y, 0)).x;
    float pUp = Pin.Load(int3(c_up.x, c_up.y, 0)).x;
    float pDown = Pin.Load(int3(c_down.x, c_down.y, 0)).x;

    if (WindFluid_SolidAtCell(c_l) > 0.5f || WindFluid_IsPressureBorderCell(c_l)) pl = 0.0f;
    if (WindFluid_SolidAtCell(c_r) > 0.5f || WindFluid_IsPressureBorderCell(c_r)) pr = 0.0f;
    if (WindFluid_SolidAtCell(c_up) > 0.5f || WindFluid_IsPressureBorderCell(c_up)) pUp = 0.0f;
    if (WindFluid_SolidAtCell(c_down) > 0.5f || WindFluid_IsPressureBorderCell(c_down)) pDown = 0.0f;

    v.x -= (pr - pl) * 0.5f;
    v.y -= (pUp - pDown) * 0.5f;

    VelOut[int2((int)ij.x, (int)ij.y)] = v;
}
