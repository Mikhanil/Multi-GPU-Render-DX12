#include "Common.hlsl"


struct VertexOut
{
	float4 PosH : SV_POSITION;
	float3 PosL : POSITION;
    float2 UV : UV;
};

struct VertexIn
{
	float3 PosL : POSITION;
	float3 NormalL : NORMAL;
	float2 TexC : TEXCOORD;
};


VertexOut SKYMAP_VS(VertexIn vin)
{
	VertexOut vout;

	// Use local vertex position as cubemap lookup vector.
	vout.PosL = vin.PosL;

	// Transform to world space.
	float4 posW = mul(float4(vin.PosL, 1.0f), objectBuffer.World);

	// Always center sky about camera.
	posW.xyz += worldBuffer.EyePosW;

	// Set z = w so that z/w = 1 (i.e., skydome always on far plane).
	vout.PosH = mul(posW, worldBuffer.ViewProj).xyww;

    vout.UV = vin.TexC;
	
	return vout;
}

float4 SKYMAP_PS(VertexOut pin) : SV_Target
{
    float4 apexColor = float4(0.0f, 0.15f, 0.66f, 1.0f);
    float4 centerColor = float4(0.81f, 0.38f, 0.66f, 1.0f);    
	
    float height;
    float4 outputColor;

    outputColor = ssaoMap.Sample(gsamAnisotropicWrap, pin.UV);

	
    return outputColor;
	
    //Determine the position on the sky dome where this pixel is located.
    height = pin.PosL.y;

    //The value ranges from -1.0f to +1.0f so change it to only positive values.
    if (height > 0.0)
    {       
       // outputColor = lerp(centerColor, apexColor, height);

        outputColor = ssaoMap.Sample(gsamAnisotropicWrap, pin.UV);
    }
	else
	{
        //outputColor = lerp(centerColor1, apexColor1, height * -1);

        outputColor = SkyMap.Sample(gsamLinearWrap, pin.PosL);
    }

	
    //Determine the gradient color by interpolating between the apex and center based on the height of the pixel in the sky dome.
   
	
	

    return outputColor;
}
