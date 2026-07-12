Texture2D SceneColor : register(t2);
SamplerState gsamLinearClamp : register(s3);

struct VertexIn
{
    float3 PosL : POSITION;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut)0.0f;

    vout.PosH = float4(vin.PosL, 1.0f);
    vout.TexC = vin.TexC;

    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    return SceneColor.Sample(gsamLinearClamp, pin.TexC);
}
