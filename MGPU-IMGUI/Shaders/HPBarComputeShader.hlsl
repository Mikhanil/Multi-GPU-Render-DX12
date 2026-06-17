// HPBarCompute_Ultimate.hlsl
cbuffer HPBarCB : register(b0)
{
    float2 TextureSize; 
    float Time; 
    float Health; 
    float4 ColorStart; 
    float4 ColorEnd; 
    float SparkleSpeed;
    float SparkleIntensity; 
    float2 StarPosMin; 
    float2 StarPosMax; 
    float DistortionPower; 
    float GlowIntensity; 
    float FractalScale;
    float FillThreshold; 
    float CrackRoughness; 
    float CrackThickness; 
};

RWTexture2D<float4> OutputTexture : register(u0);

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

float FractalNoise(float2 uv, float scale)
{
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 3; i++)
    {
        uv += float2(0.7, 1.2) * float(i);
        value += amplitude * sin(uv.x * scale + Time * 0.3) * cos(uv.y * scale + Time * 0.4);
        scale *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

float2 VortexDistortion(float2 uv, float power)
{
    float2 center = float2(0.5, 0.5);
    float2 dir = uv - center;
    float dist = length(dir);
    float angle = atan2(dir.y, dir.x) + Time * 0.5;
    float vortex = (1.0 - dist) * power;
    return uv + vortex * float2(cos(angle), sin(angle));
}

float GenerateCrack(float2 uv, float seed, float thickness, float roughness)
{
    float2 startPos = float2(0.0, 0.5);
    float2 endPos = float2(1.0, 0.5 + Rand(float2(seed, 1.0), seed) * 0.4 - 0.2);
    float crack = 0.0;
    int steps = 5;
    float2 prevPoint = startPos;
    
    for (int i = 1; i <= steps; i++)
    {
        float t = float(i) / float(steps);
        float2 currentPoint = lerp(startPos, endPos, t);
        float offset = Rand(float2(t, seed), seed + i) * roughness * (1.0 - t);
        currentPoint.y += offset;
        float segment = smoothstep(thickness, 0.0, distance(uv, lerp(prevPoint, currentPoint, uv.x)));
        crack = max(crack, segment);
        prevPoint = currentPoint;
    }
    return crack;
}

float4 SpawnStars(float2 pixelPos, float seed)
{
    float starCount = 70.0 + floor(Rand(float2(0.0, seed), seed) * 150.0);
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
        float size = lerp(12.0, 60.0, Rand(float2(float(i), seed), seed + i * 0.2));
        float2 dir = normalize(pixelPos - starPos);
        float dist = length((pixelPos - starPos) * float2(1.0, aspectRatio));
        float star = smoothstep(size, size * 0.3, dist);
        star *= (0.6 + 0.4 * sin(Time * 3.0 + i));
        float tail = smoothstep(size * 2.0, 0.0, dist) * smoothstep(0.0, size * 0.3, dist);
        tail *= (0.5 + 0.5 * cos(Time * 2.0 + i * 0.7));
        float3 color = saturate(RandColor(float2(float(i), seed), seed + i * 0.5) * 2.5);
        totalColor += (star + tail * 0.7) * color;
        totalAlpha += star + tail * 0.5;
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
    float4 outputColor = float4(0, 0, 0, 0);
    float clampedHealth = clamp(Health, 0.0, 1.0);
    bool isOverflow = (clampedHealth >= FillThreshold);
    float visualFill = isOverflow ? 1.0 : clampedHealth;

    float2 distortedUV = VortexDistortion(uv, DistortionPower * 0.1);
    distortedUV += FractalNoise(uv * 5.0, FractalScale) * 0.02;

    if (distortedUV.x <= visualFill)
    {
        float t = distortedUV.x / visualFill;
        float noise = FractalNoise(distortedUV * 10.0, 2.0) * 0.1;
        outputColor = lerp(ColorStart, ColorEnd, t + noise);

        if (isOverflow)
        {
            float crack = GenerateCrack(distortedUV, Time * 0.2, CrackThickness, CrackRoughness);
            float3 crackColor = float3(1.0, 0.3, 0.1) * crack * 5.0;
            outputColor.rgb += crackColor;
            outputColor.rgb *= 1.0 - crack * 0.3;
            float pulse = 0.8 + 0.3 * sin(Time * 8.0);
            outputColor.rgb *= pulse;
        }
        else
        {
            float pulse = 0.9 + 0.1 * sin(Time * 2.0);
            outputColor.rgb *= pulse;
        }

        float sparkle = pow(abs(FractalNoise(distortedUV * 20.0 + Time * SparkleSpeed, 3.0)), 8.0) * SparkleIntensity;
        outputColor.rgb += float3(sparkle, sparkle, sparkle) * 2.0;
        float4 stars = SpawnStars(pixelPos, Time * 0.1);
        outputColor.rgb += stars.rgb;
        outputColor.a = 1.0;
    }

    OutputTexture[DTid.xy] = saturate(outputColor);
}