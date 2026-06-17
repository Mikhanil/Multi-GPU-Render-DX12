#include "WindFluid_Common.hlsli"

Texture2D<float> DivRhs : register(t0);
Texture2D<float> Pin : register(t1);
RWTexture2D<float> Pout : register(u0);

[numthreads(8, 8, 1)]
void CS_Main(uint3 id : SV_DispatchThreadID)
{
    WF_BindGridFromR(DivRhs);

    uint2 ij = id.xy;
    if (ij.x >= WF_GridW || ij.y >= WF_GridH)
        return;

    int2 maxCell = int2((int)WF_GridW, (int)WF_GridH) - int2(1, 1);

    int2 pix = int2((int)ij.x, (int)ij.y);
    if (WindFluid_SolidAtCell(pix) > 0.5f)
    {
        Pout[pix] = 0.0f;
        return;
    }

    int2 c_l = WindFluid_ClampCell(pix + int2(-1, 0), maxCell);
    int2 c_r = WindFluid_ClampCell(pix + int2(1, 0), maxCell);
    int2 c_d = WindFluid_ClampCell(pix + int2(0, -1), maxCell);
    int2 c_u = WindFluid_ClampCell(pix + int2(0, 1), maxCell);

    float pl = Pin.Load(int3(c_l.x, c_l.y, 0)).x;
    float pr = Pin.Load(int3(c_r.x, c_r.y, 0)).x;
    float pd = Pin.Load(int3(c_d.x, c_d.y, 0)).x;
    float pu = Pin.Load(int3(c_u.x, c_u.y, 0)).x;

    if (WindFluid_SolidAtCell(c_l) > 0.5f || WindFluid_IsPressureBorderCell(c_l)) pl = 0.0f;
    if (WindFluid_SolidAtCell(c_r) > 0.5f || WindFluid_IsPressureBorderCell(c_r)) pr = 0.0f;
    if (WindFluid_SolidAtCell(c_d) > 0.5f || WindFluid_IsPressureBorderCell(c_d)) pd = 0.0f;
    if (WindFluid_SolidAtCell(c_u) > 0.5f || WindFluid_IsPressureBorderCell(c_u)) pu = 0.0f;

    float b = DivRhs.Load(int3((int)ij.x, (int)ij.y, 0)).x;

    float pNew = (pl + pr + pd + pu - b) * 0.25f;
    Pout[int2((int)ij.x, (int)ij.y)] = pNew;
}
