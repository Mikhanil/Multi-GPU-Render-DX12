// HPBarCompute_UltraHeavy.hlsl
cbuffer HPBarCB : register(b0)
{
    float2 TextureSize; // Размер текстуры
    float Time; // Время для анимации
    float Health; // Уровень здоровья (0.0 - 1.0)
    float4 ColorStart; // Начальный цвет градиента
    float4 ColorEnd; // Конечный цвет градиента
    float SparkleSpeed; // Скорость бликов
    float SparkleIntensity; // Интенсивность бликов
    float2 StarPosMin; // Область появления звёзд (min)
    float2 StarPosMax; // Область появления звёзд (max)
    float DistortionPower; // Сила искажений
    float GlowIntensity; // Интенсивность свечения
    float FractalScale; // Масштаб фрактального шума
};

RWTexture2D<float4> OutputTexture : register(u0);

// Псевдослучайные числа
float Rand(float2 uv, float seed)
{
    return frac(sin(dot(uv + seed, float2(12.9898, 78.233)) * 43758.5453));
}
float2 Rand2(float2 uv, float seed)
{
    return frac(sin(float2(dot(uv + seed, float2(127.1, 311.7)), dot(uv + seed, float2(269.5, 183.3)))) * 43758.5453);
}
float3 RandColor(float2 uv, float seed)
{
    return float3(Rand(uv, seed), Rand(uv, seed + 1.0), Rand(uv, seed + 2.0));
}

// Фрактальный шум (3 октавы)
float FractalNoise(float2 uv, float scale)
{
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 3; i++)
    {
        uv += float2(0.7, 1.2) * float(i); // Сдвигаем для разнообразия
        value += amplitude * sin(uv.x * scale + Time * 0.3) * cos(uv.y * scale + Time * 0.4);
        scale *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

// Вихревое искажение (на основе векторного поля)
float2 VortexDistortion(float2 uv, float power)
{
    float2 center = float2(0.5, 0.5);
    float2 dir = uv - center;
    float dist = length(dir);
    float angle = atan2(dir.y, dir.x) + Time * 0.5;
    float vortex = (1.0 - dist) * power;
    return uv + vortex * float2(cos(angle), sin(angle));
}

// Имитация свечения (аналог blur)
float3 FakeGlow(float2 uv, float2 center, float radius, float intensity)
{
    float dist = distance(uv, center);
    float glow = exp(-dist * radius) * intensity;
    float3 color = float3(0.8, 0.9, 1.0) * glow;
    return color;
}

// Генерация сложных звёзд с хвостами
float4 SpawnStars(float2 pixelPos, float seed)
{
    float starCount = 80.0 + floor(Rand(float2(0.0, seed), seed) * 120.0); // 80-200 звёзд
    float3 totalColor = float3(0, 0, 0);
    float totalAlpha = 0.0;
    float aspectRatio = TextureSize.x / TextureSize.y;
    float2 minPos = StarPosMin * TextureSize;
    float2 maxPos = float2(min(StarPosMax.x, Health), StarPosMax.y) * TextureSize;

    for (int i = 0; i < starCount; i++)
    {
        float2 starPos = Rand2(float2(float(i), seed), seed + i * 0.1);
        starPos = lerp(minPos, maxPos, starPos);
        starPos.x += fmod(Time * 60.0 + i * 150.0, maxPos.x - minPos.x);
        starPos.y += sin(Time * 0.7 + i * 0.5) * 15.0;

        // Размер и форма звезды
        float size = lerp(5.0, 50.0, Rand(float2(float(i), seed), seed + i * 0.2));
        float2 dir = normalize(pixelPos - starPos);
        float dist = length((pixelPos - starPos) * float2(1.0, aspectRatio));

        // Основная звезда (круг)
        float star = smoothstep(size, size * 0.3, dist);
        star *= (0.6 + 0.4 * sin(Time * 3.0 + i));

        // Хвост (анимация движения)
        float tailLength = size * 2.0;
        float tail = smoothstep(tailLength, 0.0, dist) * smoothstep(0.0, tailLength * 0.3, dist);
        tail *= (0.5 + 0.5 * cos(Time * 2.0 + i * 0.7));

        // Случайный цвет
        float3 color = saturate(RandColor(float2(float(i), seed), seed + i * 0.5) * 2.5);
        totalColor += (star + tail * 0.7) * color;
        totalAlpha += star + tail * 0.5;

        // Свечение вокруг звезды
        totalColor += FakeGlow(pixelPos / TextureSize, starPos / TextureSize, 10.0, 0.3) * star;
    }

    return float4(totalColor, totalAlpha * 3.0);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= (uint) TextureSize.x || DTid.y >= (uint) TextureSize.y)
        return;

    float2 pixelPos = float2(DTid.x, DTid.y);
    float2 uv = pixelPos / TextureSize;

    // Искажение UV (вихрь + шум)
    float2 distortedUV = VortexDistortion(uv, DistortionPower * 0.1);
    distortedUV += FractalNoise(uv * 5.0, FractalScale) * 0.02;

    float4 outputColor = float4(0, 0, 0, 0);
    float clampedHealth = clamp(Health, 0.0, 1.0);

    if (distortedUV.x <= clampedHealth)
    {
        // Градиент с фрактальным шумом
        float t = distortedUV.x / clampedHealth;
        float noise = FractalNoise(distortedUV * 10.0, 2.0) * 0.1;
        outputColor = lerp(ColorStart, ColorEnd, t + noise);

        // Блики на основе шума
        float sparkle = pow(abs(FractalNoise(distortedUV * 20.0 + Time * SparkleSpeed, 3.0)), 8.0) * SparkleIntensity;
        outputColor.rgb += float3(sparkle, sparkle, sparkle) * 2.0;

        // Пульсация с хаотичным шумом
        float pulse = 0.9 + 0.1 * sin(Time * 4.0 + distortedUV.x * 15.0 + FractalNoise(distortedUV, 1.0));
        outputColor.rgb *= pulse;

        // Звёзды с хвостами и свечением
        float4 stars = SpawnStars(pixelPos, Time * 0.1);
        outputColor.rgb += stars.rgb;
        outputColor.a = 1.0;

        // Свечение по краям
        float edge = smoothstep(0.9, 1.0, distortedUV.x / clampedHealth) * GlowIntensity;
        outputColor.rgb += edge * float3(0.7, 0.8, 1.0);
    }

    OutputTexture[DTid.xy] = saturate(outputColor);
}