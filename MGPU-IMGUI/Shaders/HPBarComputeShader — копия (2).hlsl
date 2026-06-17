// HPBarCompute.hlsl
cbuffer HPBarCB : register(b0)
{
    float2 TextureSize; // Размер текстуры в пикселях (ширина, высота)
    float Time; // Время для анимации
    float Health; // Уровень здоровья (0.0 - 1.0)
    float4 ColorStart; // Цвет начала градиента (например, зелёный)
    float4 ColorEnd; // Цвет конца градиента (например, красный)
    float SparkleSpeed; // Скорость движения бликов
    float SparkleIntensity; // Интенсивность бликов
    float2 StarPosMin; // Минимальная позиция спавна звезд (нормализованные координаты)
    float2 StarPosMax; // Максимальная позиция спавна звезд (нормализованные координаты)
};

RWTexture2D<float4> OutputTexture : register(u0);

// Генерация случайного числа на основе seed
float Random(float2 uv, float seed)
{
    return frac(sin(dot(uv + seed, float2(12.9898, 78.233))) * 43758.5453);
}

// Генерация случайного вектора 2D
float2 Random2(float2 uv, float seed)
{
    return frac(sin(float2(dot(uv + seed, float2(127.1, 311.7)),
                    dot(uv + seed, float2(269.5, 183.3)))) * 43758.5453);
}

// Генерация случайного цвета (RGB) на основе seed
float3 RandomColor(float2 uv, float seed)
{
    return float3(
        Random(uv, seed),
        Random(uv, seed + 1.0),
        Random(uv, seed + 2.0)
    );
}

// Функция для генерации случайных звезд с цветом
float4 SpawnStar(float2 pixelPos, float seed)
{
    float starCount = floor(Random(float2(0.0, seed), seed) * 40.0 + 20.0); // 20-60 звезд
    float starEffect = 0.0;
    float3 starColor = float3(0, 0, 0); // Итоговый цвет звезды (суммируется)

    // Область спавна в пиксельных координатах
    float2 minPos = StarPosMin * TextureSize;
    float2 maxPos = float2(min(StarPosMax.x, Health), StarPosMax.y) * TextureSize;

    float aspectRatio = TextureSize.x / TextureSize.y;

    for (int i = 0; i < starCount; i++)
    {
        // Генерируем положение звезды
        float2 starPos = Random2(float2(float(i), seed), seed + i * 0.1);
        starPos = lerp(minPos, maxPos, starPos);

        // Добавляем движение слева направо
        float moveSpeed = 50.0;
        starPos.x += fmod(Time * moveSpeed + i * 100.0, maxPos.x - minPos.x);
        starPos.y += sin(Time * 0.5 + i * 0.3) * 5.0; // Небольшие колебания по Y

        // Случайный размер звезды (10-30 пикселей)
        float starSize = lerp(10.0, 30.0, Random(float2(float(i), seed), seed + i * 0.2));

        // Корректируем координаты для круглой формы
        float2 adjustedPixelPos = pixelPos;
        float2 adjustedStarPos = starPos;
        adjustedPixelPos.x /= aspectRatio;
        adjustedStarPos.x /= aspectRatio;

        // Расстояние до центра звезды
        float dist = length(adjustedPixelPos - adjustedStarPos);

        // Создаем эффект кружочка (мягкое затухание)
        float star = smoothstep(starSize, starSize * 0.5, dist);
        star *= (sin(Time * 0.5 + i * 0.5) * 0.3 + 0.7); // Мерцание

        // Генерируем случайный цвет для звезды
        float3 color = RandomColor(float2(float(i), seed), seed + i * 0.5);
        color = saturate(color); // Ограничиваем значения [0, 1]

        // Добавляем вклад звезды в общий эффект
        starEffect += star;
        starColor += star * color; // Умножаем цвет на интенсивность звезды
    }

    // Нормализуем цвет (чтобы не было пересвета)
    starColor = saturate(starColor);
    return float4(starColor, starEffect * 3.0); // Возвращаем цвет + альфа (интенсивность)
}

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= (uint) TextureSize.x || DTid.y >= (uint) TextureSize.y)
        return;

    float2 pixelPos = float2(DTid.x, DTid.y);
    float2 uv = pixelPos / TextureSize;
    float4 outputColor = float4(0, 0, 0, 0);

    float clampedHealth = clamp(Health, 0.0, 1.0);

    if (uv.x <= clampedHealth)
    {
        // Градиент здоровья
        float t = uv.x / clampedHealth;
        outputColor = lerp(ColorStart, ColorEnd, t);

        // Блики
        float sparkle = Random(uv, Time * SparkleSpeed);
        sparkle = pow(sparkle, 10.0) * SparkleIntensity;
        outputColor.rgb += float3(sparkle, sparkle, sparkle);

        // Пульсация
        float pulse = sin(Time * 2.0 + uv.x * 5.0) * 0.05 + 0.95;
        outputColor.rgb *= pulse;

        // Добавляем звезды со случайным цветом
        float4 starEffect = SpawnStar(pixelPos, sin(Time * 0.05));
        outputColor.rgb += starEffect.rgb; // Добавляем цвет звезд
        outputColor.a = 1.0;
    }

    OutputTexture[DTid.xy] = outputColor;
}