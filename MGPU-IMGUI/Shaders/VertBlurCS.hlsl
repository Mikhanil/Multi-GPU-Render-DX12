RWTexture2D<float4> gOutput : register(u0);
Texture2D gInput : register(t0);

cbuffer cbBlurSettings : register(b0)
{
    uint gWidth;
    uint gHeight;
    uint gBlurRadius;
    uint gPass;
};

[numthreads(16, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= gWidth || DTid.y >= gHeight)
        return;

    float4 color = float4(0, 0, 0, 0);
    int count = 0;
    for (int i = -gBlurRadius; i <= gBlurRadius; i++)
    {
        int y = clamp(int(DTid.y) + i, 0, gHeight - 1);
        color += gInput[uint2(y, DTid.x)];
        count++;
    }
    gOutput[DTid.xy] = color / count;
}