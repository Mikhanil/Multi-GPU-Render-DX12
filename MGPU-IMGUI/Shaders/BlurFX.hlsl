RWTexture2D<float4> gOutput : register(u0);
Texture2D<float4> gInput : register(t0);

cbuffer cbBlurSettings : register(b0)
{
    uint gWidth;
    uint gHeight;
    uint gBlurRadius;
    uint padding; // Выравнивание до 16 байт (рекомендуется для константных буферов)
};

[numthreads(8, 8, 1)] // Для 2D задач удобнее двумерный блок
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    // Отсекаем потоки, вышедшие за границы текстуры
    if (DTid.x >= gWidth || DTid.y >= gHeight)
        return;

    float4 color = float4(0, 0, 0, 0);
    int count = 0;
    int radius = int(gBlurRadius);

    // 2D обход: собираем квадрат пикселей вокруг центрального
    for (int y = -radius; y <= radius; y++)
    {
        for (int x = -radius; x <= radius; x++)
        {
            // Ограничиваем координаты, чтобы не выйти за края текстуры (Clamp)
            int clampedX = clamp(int(DTid.x) + x, 0, int(gWidth) - 1);
            int clampedY = clamp(int(DTid.y) + y, 0, int(gHeight) - 1);

            color += gInput[uint2(clampedX, clampedY)];
            count++;
        }
    }
    
    // Записываем усредненный результат
    gOutput[DTid.xy] = color / count;
}