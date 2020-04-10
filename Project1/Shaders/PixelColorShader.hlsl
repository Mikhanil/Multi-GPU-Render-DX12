
struct VertexIn
{
    float4 Pos : SV_POSITION;
    float4 Color : COLOR;
};

float4 main(VertexIn pin) : SV_Target
{
    //clip(pin.Color.r - 0.5f);
    return pin.Color;
}