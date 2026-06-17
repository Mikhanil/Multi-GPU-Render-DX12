// GradientCompute.hlsl
// Версия шейдера 5.1

struct GradientData
{
    float4 ColorStart;
    float4 ColorEnd;
    float2 TextureSize;
    float Time;
    float ColorShiftSpeed;
    float NoiseIntensity;
    float Smoothness;
};

ConstantBuffer<GradientData> gradientData : register(b0);
RWTexture2D<float4> OutputTexture : register(u0);

float gradientNoise(float2 uv)
{
    float2 i = floor(uv);
    float2 f = frac(uv);
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(
        lerp(frac(sin(dot(i, float2(12.9898, 78.233))) * 43758.5453),
               frac(sin(dot(i + float2(1.0, 0.0), float2(12.9898, 78.233))) * 43758.5453), u.x),
        lerp(frac(sin(dot(i + float2(0.0, 1.0), float2(12.9898, 78.233))) * 43758.5453),
               frac(sin(dot(i + float2(1.0, 1.0), float2(12.9898, 78.233))) * 43758.5453), u.x),
        u.y);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    float2 uv = float2(DTid.xy) / gradientData.TextureSize;
    
    float phase = gradientData.Time * gradientData.ColorShiftSpeed;
    float pingPong = abs(frac(phase * 0.5) * 2.0 - 1.0); // Плавные колебания 0?1?0
    
    float t = pow(pingPong, gradientData.Smoothness) /
              (pow(pingPong, gradientData.Smoothness) + pow(1.0 - pingPong, gradientData.Smoothness));
    
    float4 color = lerp(gradientData.ColorStart, gradientData.ColorEnd, t);
    
    float noise = gradientNoise(uv * 5.0 + float2(phase, 0.0)) * 0.5;
    noise += gradientNoise(uv * 10.0 + float2(phase * 1.3, 0.0)) * 0.25;
    color.rgb += (noise - 0.5) * gradientData.NoiseIntensity;
    
    color.rgb = saturate(color.rgb);
    
    OutputTexture[DTid.xy] = color;
}