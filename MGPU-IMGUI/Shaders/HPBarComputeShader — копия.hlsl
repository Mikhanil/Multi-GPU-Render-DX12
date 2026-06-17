// HPBarCompute.hlsl
cbuffer HPBarCB : register(b0)
{
    float2 TextureSize; // Размер текстуры (ширина, высота)
    float Time; // Время для анимации
    float Health; // Уровень здоровья (0.0 - 1.0)
    float4 ColorStart; // Цвет начала градиента (например, зелёный)
    float4 ColorEnd; // Цвет конца градиента (например, красный)
    float SparkleSpeed; // Скорость движения бликов
    float SparkleIntensity; // Интенсивность бликов
};

RWTexture2D<float4> OutputTexture : register(u0);

float3 Random(float2 uv, float seed)
{
    return frac(sin(dot(uv + seed, float2(12.9898, 78.233))) * 43758.5453);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    // Проверяем, что координаты в пределах текстуры
    if (DTid.x >= (uint) TextureSize.x || DTid.y >= (uint) TextureSize.y)
        return;

    float2 uv = float2(DTid.x, DTid.y) / TextureSize;
    float4 outputColor = float4(0, 0, 0, 0); // Прозрачный фон для пустой части

    // Определяем заполненную часть бара (слева направо)
    if (uv.x <= Health)
    {
        // Градиент от ColorStart к ColorEnd
        float t = uv.x / Health; // Нормализуем по заполненной части
        outputColor = lerp(ColorStart, ColorEnd, t);

        // Добавляем анимированные блики
        float sparkle = Random(uv, Time * SparkleSpeed).x;
        sparkle = pow(sparkle, 10.0) * SparkleIntensity; // Делаем блики резкими
        outputColor.rgb += float3(sparkle, sparkle, sparkle);

        // Добавляем эффект пульсации
        float pulse = sin(Time * 2.0 + uv.x * 5.0) * 0.05 + 0.95;
        outputColor.rgb *= pulse;

        outputColor.a = 1.0; // Полная непрозрачность
    }

    // Записываем результат
    OutputTexture[DTid.xy] = outputColor;
}