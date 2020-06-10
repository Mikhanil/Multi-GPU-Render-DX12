#include "Common.hlsl"

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 TangentU : TANGENT;
};

struct VertexOut
{
    float4 PosView : SV_POSITION;
    float4 ShadowPosH : POSITION0;
    float3 PosWorld : POSITION1;
    float3 NormalWorld : NORMAL;
    float3 TangentWorld : TANGENT;
    float2 TexC : TEXCOORD;
};


VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut) 0.0f;

    MaterialData matData = materialData[objectBuffer.materialIndex];
	
    float4 posW = mul(float4(vin.PosL, 1.0f), objectBuffer.World);
    vout.PosWorld = posW.xyz;
     
    vout.NormalWorld = mul(vin.NormalL, (float3x3) objectBuffer.World);

    vout.PosView = mul(posW, worldBuffer.ViewProj);

    vout.TangentWorld = mul(vin.TangentU, (float3x3) objectBuffer.World);
    
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), objectBuffer.TexTransform);
    vout.TexC = mul(texC, matData.MatTransform).xy;

    // Generate projective tex-coords to project shadow map onto scene.
    vout.ShadowPosH = mul(posW, worldBuffer.ShadowTransform);
	
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    MaterialData matData = materialData[objectBuffer.materialIndex];
    float4 diffuseAlbedo = matData.DiffuseAlbedo;
    float3 fresnelR0 = matData.FresnelR0;
    float roughness = matData.Roughness;
    uint diffuseMapIndex = matData.DiffuseMapIndex;
    uint normalMapIndex = matData.NormalMapIndex;
	
    diffuseAlbedo *= texturesMaps[diffuseMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);
    
#ifdef ALPHA_TEST	
	clip(diffuseAlbedo.a - 0.1f);
#endif
        
    pin.NormalWorld = normalize(pin.NormalWorld);

    float4 normalMapSample = texturesMaps[normalMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);
    float3 bumpedNormalW = NormalSampleToWorldSpace(normalMapSample.rgb, pin.NormalWorld, pin.TangentWorld);

	
    //bumpedNormalW = pin.NormalWorld;
	
    // Vector from point being lit to eye. 
    float3 toEyeW = normalize(worldBuffer.EyePosW - pin.PosWorld);
    float distToEye = length(toEyeW);
    toEyeW /= distToEye;

    // Light terms.
    float4 ambient = worldBuffer.AmbientLight * diffuseAlbedo;


    // Only the first light casts a shadow.
    float3 shadowFactor = float3(1.0f, 1.0f, 1.0f);
    shadowFactor[0] = CalcShadowFactor(pin.ShadowPosH);

	
    const float shininess = (1.0f - roughness) * normalMapSample.a;
    Material mat = { diffuseAlbedo, fresnelR0, shininess };
    float4 directLight = ComputeLighting(worldBuffer.Lights, mat, pin.PosWorld,
        bumpedNormalW, toEyeW, shadowFactor);

    float4 litColor = ambient + directLight;

#ifdef FOG
	float fogAmount = saturate((distToEye - worldBuffer.gFogStart) / worldBuffer.gFogRange);
    litColor = lerp(litColor, worldBuffer.gFogColor, fogAmount);
#endif    

	// Add in specular reflections.
    float3 r = reflect(-toEyeW, bumpedNormalW);
    float4 reflectionColor = SkyMap.Sample(gsamLinearWrap, r);
    float3 fresnelFactor = SchlickFresnel(fresnelR0, bumpedNormalW, r);
    litColor.rgb += shininess * fresnelFactor * reflectionColor.rgb;
	
    litColor.a = diffuseAlbedo.a;

    return litColor;
}

