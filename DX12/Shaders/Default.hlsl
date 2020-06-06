#include "Common.hlsl"

Texture2D diffuseMaps[] : register(t0);


SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerState gsamCubeTextureWrap : register(s6);


struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosView : SV_POSITION;
    float3 PosWorld : POSITION;
    float3 NormalWorld : NORMAL;
    float2 TexC : TEXCOORD;
};


VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;
	
    float4 posW = mul(float4(vin.PosL, 1.0f), objectBuffer.World);
    vout.PosWorld = posW.xyz;
     
    vout.NormalWorld = mul(vin.NormalL, (float3x3) objectBuffer.World);

    vout.PosView = mul(posW, worldBuffer.ViewProj);
	
    
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), objectBuffer.TexTransform);
    vout.TexC = mul(texC, materialData[objectBuffer.materialIndex].MatTransform).
    xy;
	
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    MaterialData materialBuffer = materialData[objectBuffer.materialIndex];
    float4 diffuseAlbedo = diffuseMaps[materialBuffer.DiffuseMapIndex].Sample(gsamAnisotropicWrap, pin.TexC) * materialBuffer.DiffuseAlbedo;
    
#ifdef ALPHA_TEST	
	clip(diffuseAlbedo.a - 0.1f);
#endif
        
    pin.NormalWorld = normalize(pin.NormalWorld);

    // Vector from point being lit to eye. 
    float3 toEyeW = normalize(worldBuffer.EyePosW - pin.PosWorld);
    float distToEye = length(toEyeW);
    toEyeW /= distToEye;

    // Light terms.
    float4 ambient = worldBuffer.AmbientLight * diffuseAlbedo;

    const float shininess = 1.0f - materialBuffer.Roughness;
    Material mat = { diffuseAlbedo, materialBuffer.FresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(worldBuffer.Lights, mat, pin.PosWorld,
        pin.NormalWorld, toEyeW, shadowFactor);

    float4 litColor = ambient + directLight;

#ifdef FOG
	float fogAmount = saturate((distToEye - worldBuffer.gFogStart) / worldBuffer.gFogRange);
    litColor = lerp(litColor, worldBuffer.gFogColor, fogAmount);
#endif    
    
    litColor.a = diffuseAlbedo.a;

    return litColor;
}

