// GradientOverlayCompute.hlsl
// Версия шейдера 5.1

struct GradientData
{
    float4 ColorStart; // Стартовый цвет градиента
    float4 ColorEnd; // Конечный цвет градиента
    float2 TextureSize; // Размер текстуры
    float Time; // Время для анимации
    float ColorShiftSpeed; // Скорость изменения (0.1-0.5)
    float NoiseIntensity; // Интенсивность шума (0.0-0.2)
    float Smoothness; // Плавность перехода (1.0-3.0)
    float BlendFactor; // Коэффициент смешивания (0.0-1.0)
};

ConstantBuffer<GradientData> gradientData : register(b0);
Texture2D<float4> InputTexture : register(t0); // Входная текстура
RWTexture2D<float4> OutputTexture : register(u0); // Выходная текстура

// Улучшенный шум с плавными переходами
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
    
    // Получаем цвет из исходной текстуры
    float4 baseColor = InputTexture[DTid.xy];
    
    // Автоколебательная система (0→1→0)
    float phase = gradientData.Time * gradientData.ColorShiftSpeed;
    float pingPong = abs(frac(phase * 0.5) * 2.0 - 1.0);
    
    // Сигмоидное сглаживание
    float t = pow(pingPong, gradientData.Smoothness) /
              (pow(pingPong, gradientData.Smoothness) + pow(1.0 - pingPong, gradientData.Smoothness));
    
    // Цвет градиента
    float4 gradientColor = lerp(gradientData.ColorStart, gradientData.ColorEnd, t);
    
    // Многослойный шум
    float noise = gradientNoise(uv * 5.0 + float2(phase, 0.0)) * 0.5;
    noise += gradientNoise(uv * 10.0 + float2(phase * 1.3, 0.0)) * 0.25;
    gradientColor.rgb += (noise - 0.5) * gradientData.NoiseIntensity;
    gradientColor.rgb = saturate(gradientColor.rgb);
    
    // Смешивание с исходной текстурой
    float4 finalColor = lerp(baseColor, gradientColor, gradientData.BlendFactor);
    
    // Запись результата
    OutputTexture[DTid.xy] = finalColor;
}