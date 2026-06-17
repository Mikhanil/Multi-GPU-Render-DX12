#include "Bruneton.hlsl"
#include "BrunetonDefinitions.hlsl"
#include "BrunetonFunctions.hlsl"

// Объявление текстуры в начале файла
RWTexture2D<float4> TransmittanceLutOut : register(u0);

cbuffer LUTConstants : register(b0, space1)
{
    uint2 TransmittanceLutDimensions;
    float2 InvTransmittanceLutDimensions;
}

[numthreads(32, 32, 1)]
void TransmittanceLutCS_Bruneton(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // Получаем размеры текстуры из константного буфера
    uint width = TransmittanceLutDimensions.x;
    uint height = TransmittanceLutDimensions.y;
    
    // Проверяем границы
    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
        return;
    
    // Вычисляем UV координаты (пиксельные координаты)
    float2 pixPos = float2(dispatchThreadId.xy);
    
    // Получаем параметры атмосферы
    // Bruneton.hlsl
    AtmosphereParameters Parameters = GetAtmosphereParameters();
    
    // Вычисляем transmittance
    // BrunetonFunctions.hlsl
    float3 transmittance = ComputeTransmittanceToTopAtmosphereBoundaryTexture(Parameters, pixPos);
    
    // Записываем результат в текстуру (UAV)
    TransmittanceLutOut[dispatchThreadId.xy] = float4(transmittance, 1.0);
}

/*
float4 TransmittanceLutPS(SlicedVertexOut Input) : SV_TARGET
{
	float2 pixPos = Input.PosH.xy;

	AtmosphereParameters Parameters = GetAtmosphereParameters();
	float3 transmittance = ComputeTransmittanceToTopAtmosphereBoundaryTexture(Parameters, pixPos);

	return float4(transmittance, 1.0);
}
*/
