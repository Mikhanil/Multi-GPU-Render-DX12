// Precision-adjusted variations of https://www.shadertoy.com/view/4djSRW
// Precision-adjusted variations of https://www.shadertoy.com/view/4tdSWr

struct CloudGeneratorData
{
    float CloudCover;
    float CloudAlpha;
    float SkyTint;
    float TotalTime;

    float CloudScale;
    float CloudSpeed;
    float CloudDark;
    float CloudLight;

    float4 SkyColor1;
    float4 SkyColor2;
    float4 CloudColor;

    float2 RenderTargetSize;
    float2 Padding1;
};


ConstantBuffer<CloudGeneratorData> generatorBuffer : register(b0);

RWTexture2D<float4> output : register(u0);


static const float2x2 RotateMatrix = float2x2(1.6, 1.2, -1.2, 1.6);


float2 hash(float2 p)
{
    p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
    return -1.0 + 2.0 * frac(sin(p) * 43758.5453123);
}

float noise(in float2 p)
{
    static const float K1 = 0.366025404; // (sqrt(3)-1)/2;
    static const float K2 = 0.211324865; // (3-sqrt(3))/6;
    float2 i = floor(p + (p.x + p.y) * K1);
    float2 a = p - i + (i.x + i.y) * K2;
    float2 o = (a.x > a.y) ? float2(1.0, 0.0) : float2(0.0, 1.0);
    //float2 of = 0.5 + 0.5*float2(sign(a.x-a.y), sign(a.y-a.x));
    float2 b = a - o + K2;
    float2 c = a - 1.0 + 2.0 * K2;
    float3 h = max(0.5 - float3(dot(a, a), dot(b, b), dot(c, c)), 0.0);
    float3 n = h * h * h * h * float3(dot(a, hash(i + 0.0)), dot(b, hash(i + o)), dot(c, hash(i + 1.0)));
    return dot(n, float3(70.0, 70.0, 70.0));
}

float fbm(float2 n)
{
    float total = 0.0, amplitude = 0.1;
    for (int i = 0; i < 7; i++)
    {
        total += noise(n) * amplitude;
        n = mul(RotateMatrix, n);
        amplitude *= 0.4;
    }
    return total;
}


[numthreads(32, 32, 1)]
void CS(uint3 DTid : SV_DispatchThreadID)
{
    float2 p = DTid.xy / generatorBuffer.RenderTargetSize.xy;
    float2 uv = p * float2(generatorBuffer.RenderTargetSize.x / generatorBuffer.RenderTargetSize.y, 1.0);

    float currentSpeed = generatorBuffer.TotalTime * generatorBuffer.CloudSpeed;
    float q = fbm(uv * generatorBuffer.CloudScale * 0.5);

    //ridged noise shape
    float r = 0.0;
    uv *= generatorBuffer.CloudScale;
    uv -= q - currentSpeed;
    float weight = 0.8;
    for (int i = 0; i < 8; i++)
    {
        r += abs(weight * noise(uv));
        uv = mul(RotateMatrix, uv) + currentSpeed;
        weight *= 0.7;
    }

    //noise shape
    float f = 0.0;
    uv = p * float2(generatorBuffer.RenderTargetSize.x / generatorBuffer.RenderTargetSize.y, 1.0);
    uv *= generatorBuffer.CloudScale;
    uv -= q - currentSpeed;
    weight = 0.7;
    for (int i = 0; i < 8; i++)
    {
        f += weight * noise(uv);
        uv = mul(RotateMatrix, uv) + currentSpeed;
        weight *= 0.6;
    }

    f *= r + f;

    //noise colour
    float c = 0.0;
    currentSpeed = generatorBuffer.TotalTime * generatorBuffer.CloudSpeed * 2.0;
    uv = p * float2(generatorBuffer.RenderTargetSize.x / generatorBuffer.RenderTargetSize.y, 1.0);
    uv *= generatorBuffer.CloudScale * 2.0;
    uv -= q - currentSpeed;
    weight = 0.4;
    for (int i = 0; i < 7; i++)
    {
        c += weight * noise(uv);
        uv = mul(RotateMatrix, uv) + currentSpeed;
        weight *= 0.6;
    }

    //noise ridge colour
    float c1 = 0.0;
    currentSpeed = generatorBuffer.TotalTime * generatorBuffer.CloudSpeed * 3.0;
    uv = p * float2(generatorBuffer.RenderTargetSize.x / generatorBuffer.RenderTargetSize.y, 1.0);
    uv *= generatorBuffer.CloudScale * 3.0;
    uv -= q - currentSpeed;
    weight = 0.4;
    for (int i = 0; i < 7; i++)
    {
        c1 += abs(weight * noise(uv));
        uv = mul(RotateMatrix, uv) + currentSpeed;
        weight *= 0.6;
    }

    c += c1;

    float4 skyColor = lerp(generatorBuffer.SkyColor2, generatorBuffer.SkyColor1, p.y);
    float4 cloudColor = generatorBuffer.CloudColor * clamp((generatorBuffer.CloudDark + generatorBuffer.CloudLight * c),
                                                           0.0, 1.0);

    f = generatorBuffer.CloudCover + generatorBuffer.CloudAlpha * f * r;

    float4 result = lerp(skyColor, clamp(generatorBuffer.SkyTint * skyColor + cloudColor, 0.0, 1.0),
                         clamp(f + c, 0.0, 1.0));

    output[DTid.xy] = result;
}
