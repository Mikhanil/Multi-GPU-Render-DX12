
struct ConstantObject
{
    float4x4 gWorldViewProj;
    float4 pulseColor;
    uint time;
};

ConstantBuffer<ConstantObject> cbPerObject : register(b0);

struct VertexIn
{
	float3 Pos  : POSITION;
    float4 Color : COLOR;
};


struct VertexOut
{
	float4 Pos  : SV_POSITION;
    float4 Color : COLOR;
};

VertexOut main(VertexIn vin)
{
    VertexOut vout;
	
    //vin.Pos.xy += 0.5f * sin(vin.Pos.x) * sin(3.0f * cbPerObject.time);
    //vin.Pos.z *= 0.6f + 0.4f * sin(2.0f * cbPerObject.time);
    
    const float pi = 3.14159;
   // Oscillate a value in [0,1] over time using a sine function. 
    float s = 0.5f * sin(2 * cbPerObject.time - 0.25f * pi) + 0.5f;
   // Linearly interpolate between pin.Color and gPulseColor based on   // parameter s.   
    float4 c = lerp(vin.Color, cbPerObject.pulseColor, s);
    
    vout.Pos = mul(float4(vin.Pos, 1.0f), cbPerObject.gWorldViewProj);
	
    vout.Color = c;
    
    return vout;
}



