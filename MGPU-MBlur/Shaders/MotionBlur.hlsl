#define _ALLOW_COMPILER_AND_LINKER_FEEDBACK_

#ifndef BlockSizeX
#define BlockSizeX 8
#endif

#ifndef BlockSizeY
#define BlockSizeY 8
#endif

#ifndef NMRadius
#define NMRadius 2
#endif

#ifndef TileSizeDebug
#define TileSizeDebug 8
#endif

cbuffer MatricesCb : register(b0)
{
    float4x4 InverseProjMatrix_;
    float4x4 ReprojectionMatrix_;
    
    float2 gTexSizeV; // {width, height}
    
    float2 gNearFar;// почему-то равны 0, 0
    
    float2 gTileCount; // size of tile grid (width/TileSize, height/TileSize)
    
    float MaxVelocity;
};

SamplerState samLinear : register(s0);
SamplerState samPoint : register(s1);

Texture2D<float> currentDepth : register(t0);
RWTexture2D<float2> VelocityOut : register(u0);

Texture2D<float2> velocityBuffer : register(t1);
RWTexture2D<float2> tileMaxOut : register(u1);

Texture2D<float2> tileMaxIn : register(t2);
RWTexture2D<float2> neighbourMaxOut : register(u2);

float LinearizeDepthV(float depth)
{
    float gNear = gNearFar.x;
    float gFar = gNearFar.y;
    return (gNear * gFar) / (gFar - depth * (gFar - gNear));
}

[numthreads(BlockSizeX, BlockSizeY, 1)]
void velocityCS(uint3 DTid : SV_DispatchThreadID)
{
   // return;
    int2 pix = int2(DTid.xy);

    // bounds check
    if (pix.x >= gTexSizeV.x || pix.y >= gTexSizeV.y)
    {
        VelocityOut[pix] = float2(0, 0);
        return;
    }

        // --- читаем depth ---
    float depth = currentDepth.Load(int3(pix, 0)).r;
    depth = LinearizeDepthV(depth); // убирает ступенчатость, но можно и без этого

    // --- 1) UV и clip space ---
    float2 invSize = 1.0 / float2(gTexSizeV);
    float2 uv = (float2(pix) + 0.5f) * invSize;
    float z = depth; //depth * 2.0f - 1.0f;
    //float z = depth * 2.0f - 1.0f;
    
    
    // сначала восстанавливаем view-space / clip-space через inverse projection
    float4 clip = float4(uv * 2 - 1, z, 1); // ndc
    float4 viewPos = mul(InverseProjMatrix_, clip);
    viewPos /= viewPos.w;

    // --- 2) Репроекция в прошлый кадр ---
    float4 prevClip = mul(ReprojectionMatrix_, viewPos);
    prevClip /= prevClip.w;

    // --- 3) Переводим в UV с Y переворотом (DirectX) ---
    float2 currentVP = viewPos.xy * float2(0.5f, -0.5f) + 0.5f;
    float2 prevVP = prevClip.xy * float2(0.5f, -0.5f) + 0.5f;

    // --- 4) Velocity в пикселях ---
    float2 velocity = (currentVP - prevVP) * gTexSizeV;

    VelocityOut[pix] = velocity;

}

[numthreads(8, 8, 1)]
void tilemaxCS(uint3 DTid : SV_DispatchThreadID)
{
    uint2 tileID = DTid.xy;

    uint2 basePixel = tileID * 8;

    float3 maxVel = float3(0, 0, 0);

    for (uint y = 0; y < 8; y++)
        for (uint x = 0; x < 8; x++)
        {
            uint2 p = basePixel + uint2(x, y);
            if (p.x >= gTexSizeV.x || p.y >= gTexSizeV.y)
                continue;

            float2 v = velocityBuffer[p];
            float mag = dot(v, v);

            if (mag > maxVel.z)
                maxVel = float3(v, mag);
        }

    tileMaxOut[tileID] = maxVel.xy;
}

[numthreads(8, 8, 1)]
void neighbourmaxCS(uint3 DTid : SV_DispatchThreadID)
{
    int2 loadXY = int2(DTid.xy);
    int2 storeXY = loadXY;

    float3 maxVelocity = float3(0, 0, 0);
    float3 initialVelocity;

    // Берём текущую скорость из tileMax
    initialVelocity.xy = tileMaxIn[loadXY];
    initialVelocity.z = dot(initialVelocity.xy, initialVelocity.xy);

    const int STEPS = 5;

    // Перебираем соседние тайлы
    for (int j = -STEPS; j <= STEPS; ++j)
    {
        for (int i = -STEPS; i <= STEPS; ++i)
        {
            int2 n = loadXY + int2(i, j);

            // clamp по экрану / размеру тайлов
            n = clamp(n, int2(0, 0), gTileCount - 1);

            float2 v = tileMaxIn[n];
            float m = dot(v, v);

            if (m > maxVelocity.z)
                maxVelocity = float3(v, m);
        }
    }

    // Корректировка ориентации
    float factor = saturate(initialVelocity.z / maxVelocity.z);

    if (factor > 0.01f)
    {
        maxVelocity.xy = lerp(maxVelocity.xy, normalize(initialVelocity.xy) * sqrt(maxVelocity.z), factor);
    }

    neighbourMaxOut[storeXY] = maxVelocity.xy;
}

////

RWTexture2D<float4> HdrTarget : register(u3);
Texture2D<float2> neighbourMaxIn : register(t3);
Texture2D<float4> HdrSource : register(t4);
//Texture2D<float2> velocityBuf : register(t5); // t1
//Texture2D<float> depth : register(t6); // t0

// Ограничение скорости
float2 ClampVelocity(float2 velocity, float maxVelocity)
{
    float len = length(velocity);
    return (len > 0.001) ? min(len, maxVelocity) * (velocity / len) : float2(0, 0);
}

// Функции soft blending
float cone(float2 pX, float2 pY, float2 v)
{
    return saturate(1 - distance(pX, pY) / length(v));
}

float cylinder(float2 pX, float2 pY, float2 v)
{
    float L = length(v);
    float D = distance(pX, pY);
    return 1 - smoothstep(0.95f * L, 1.05f * L, D);
}

float softDepthCompare(float za, float zb)
{
    const float SOFT_Z_EXTENT = 0.01; //0.01;
    return saturate(1 - (zb - za) / SOFT_Z_EXTENT);
}

float2 ClampUVTile(float2 uv, int2 pixelCoord)
{
    float2 tileSizeUV = float2(TileSizeDebug / gTexSizeV.x, TileSizeDebug / gTexSizeV.y);
    int2 tileIndex = int2(pixelCoord / TileSizeDebug); // определяем, в каком тайле пиксель

    float2 marginUV = float2(1.0 / gTexSizeV.x, 1.0 / gTexSizeV.y);
    float2 tileMin = float2(tileIndex) * tileSizeUV + marginUV;
    float2 tileMax = tileMin + tileSizeUV - 2.0f * marginUV;

    return clamp(uv, tileMin, tileMax);
}

// Основной расчёт Motion Blur
float3 CalcMotionBlur(float2 uv, int2 pixelCoord)
{
    float eps = 1.0 / 8192.0f;

    float2 vn = neighbourMaxIn.SampleLevel(samLinear, uv, 0) * 5;
    vn = ClampVelocity(vn, 100);//MaxVelocity);
    
    float2 vx = velocityBuffer.SampleLevel(samPoint, uv, 0);
    float3 cx = HdrSource.SampleLevel(samPoint, uv, 0).rgb;
    float zx = LinearizeDepthV(currentDepth.SampleLevel(samPoint, uv, 0).r);

    //return float3(length(vn), 0, 0);
    //return float3(vn, 1);
    //return float3(vx, 1);
    //return cx;
    //return float3(depth.SampleLevel(samPoint, uv, 0).r, 0, 0);
    
    //float threshold = 0.001f; //1 / 4096.0f;
    float threshold = 1 / 4096.0f;
    
    
    if (length(vn) < threshold)
        return cx;
    //return float3(0, 0, 0);

    float noiseValue = 0;
    
    float weight = 1.0 / (length(vx) + eps);
    float3 sum = cx * weight;

    int STEPS = 10;
    float invSteps = 1.0 / STEPS;

    [loop]
    for (int i = -STEPS; i <= STEPS; i++)
    {
        if (i == 0)
            continue;

        float t = ((float) i + noiseValue) / (float) (STEPS) / 2.0f;
        t *= 5; //3;
        
        float2 sampleUV = uv + (vn / gTexSizeV) * t;
        sampleUV = clamp(sampleUV, 0.0, 1.0);
        //sampleUV = ClampUVTile(sampleUV, pixelCoord); // это точно нужно?

        float3 cy = HdrSource.SampleLevel(samPoint, sampleUV, 0).rgb;
        float zy = LinearizeDepthV(currentDepth.SampleLevel(samPoint, sampleUV, 0).r);
        float2 vy = velocityBuffer.SampleLevel(samPoint, sampleUV, 0);

        float f = softDepthCompare(zx, zy);
        float b = softDepthCompare(zy, zx);

        float ay = f * cone(sampleUV, uv, vy)
                 + b * cone(uv, sampleUV, vx)
                 + cylinder(sampleUV, uv, vy) * cylinder(uv, sampleUV, vx) * 2.0;

        weight += ay;
        sum += cy * ay;
    }

    return sum / weight;
}

// Основной расчёт Motion Blur
float3 CalcMotionBlurShort(float2 uv, int2 pixelCoord)
{
    float eps = 1.0 / 8192.0f;

    float2 vn = neighbourMaxIn.SampleLevel(samLinear, uv, 0) * 5;
    vn = ClampVelocity(vn, 100); //MaxVelocity);
    
    //float2 vx = velocityBuffer.SampleLevel(samPoint, uv, 0);
    float3 cx = HdrSource.SampleLevel(samPoint, uv, 0).rgb;
    float zx = LinearizeDepthV(currentDepth.SampleLevel(samPoint, uv, 0).r);

    //return float3(length(vn), 0, 0);
    //return float3(vn, 1);
    //return float3(vx, 1);
    //return cx;
    //return float3(depth.SampleLevel(samPoint, uv, 0).r, 0, 0);
    
    //float threshold = 0.001f; //1 / 4096.0f;
    float threshold = 1 / 4096.0f;
    
    
    if (length(vn) < threshold)
        return cx;
    //return float3(0, 0, 0);

    float noiseValue = 0;
    
    float weight = 1.0 / (length(vn) + eps);
    float3 sum = cx * weight;

    int STEPS = 10;
    float invSteps = 1.0 / STEPS;

    [loop]
    for (int i = -STEPS; i <= STEPS; i++)
    {
        if (i == 0)
            continue;

        float t = ((float) i + noiseValue) / (float) (STEPS) / 2.0f;
        t *= 5; //3;
        
        float2 sampleUV = uv + (vn / gTexSizeV) * t;
        //sampleUV = clamp(sampleUV, 0.0, 1.0); // это временно убираю
        //sampleUV = ClampUVTile(sampleUV, pixelCoord); // это точно нужно?

        float3 cy = HdrSource.SampleLevel(samPoint, sampleUV, 0).rgb;
        float zy = LinearizeDepthV(currentDepth.SampleLevel(samPoint, sampleUV, 0).r);
        //float2 vy = velocityBuffer.SampleLevel(samPoint, sampleUV, 0);

        float f = softDepthCompare(zx, zy);
        float b = softDepthCompare(zy, zx);

        float ay = f * cone(sampleUV, uv, vn)
                 + b * cone(uv, sampleUV, vn)
                 + cylinder(sampleUV, uv, vn) * cylinder(uv, sampleUV, vn) * 2.0;

        weight += ay;
        sum += cy * ay;
    }

    return sum / weight;
}

[numthreads(BlockSizeX, BlockSizeY, 1)]
void mbCS(uint3 DTid : SV_DispatchThreadID)
{
    int2 pixelCoord = int2(DTid.xy);
    
    if ((pixelCoord.x >= gTexSizeV.x) || (pixelCoord.y >= gTexSizeV.y) || (pixelCoord.x < 0) || (pixelCoord.y < 0))
        return;
    
    float2 uv = (float2(pixelCoord) + 0.5f) * (1.0f / float2(gTexSizeV));
    
    //float3 finalColor = CalcMotionBlur(uv, pixelCoord)
    float3 finalColor = CalcMotionBlurShort(uv, pixelCoord);
    HdrTarget[pixelCoord] = float4(finalColor, 1.0f);
  //  HdrTarget[pixelCoord] = (velocityBuffer, 0.0f, 1.0f); 
}