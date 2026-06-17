#include "WindFluid_Common.hlsli"

Texture2D<float2> VelField : register(t0);
RWTexture2D<float> DivOut : register(u0);

[numthreads(8, 8, 1)]
void CS_Main(uint3 id : SV_DispatchThreadID)
{
    WF_BindGridFromRG(VelField);

    uint2 ij = id.xy;
    if (ij.x >= WF_GridW || ij.y >= WF_GridH)
        return;

    int2 maxCell = int2((int)WF_GridW, (int)WF_GridH) - int2(1, 1);

    int2 pix = int2((int)ij.x, (int)ij.y);
    if (WindFluid_SolidAtCell(pix) > 0.5f)
    {
        DivOut[pix] = 0.0f;
        return;
    }

    int2 c_xp = WindFluid_ClampCell(pix + int2(1, 0), maxCell);
    int2 c_xm = WindFluid_ClampCell(pix + int2(-1, 0), maxCell);
    int2 c_up = WindFluid_ClampCell(WindFluid_CellUp(pix), maxCell);
    int2 c_down = WindFluid_ClampCell(WindFluid_CellDown(pix), maxCell);

    float2 vxp = WindFluid_LoadVelWithSolid(VelField, c_xp);
    float2 vxm = WindFluid_LoadVelWithSolid(VelField, c_xm);
    float2 vUp = WindFluid_LoadVelWithSolid(VelField, c_up);
    float2 vDown = WindFluid_LoadVelWithSolid(VelField, c_down);

    float div = ((vxp.x - vxm.x) + (vUp.y - vDown.y)) * 0.5f;
    DivOut[int2((int)ij.x, (int)ij.y)] = div;
}
