#include "Common.hlsl"


//Texture2DArray gTreeMapArray : register(t1);

struct VertexIn
{
    float3 PosW : POSITION;
    float2 SizeW : SIZE;
};

struct VertexOut
{
    float3 CenterW : POSITION;
    float2 SizeW : SIZE;
};

struct GeoOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
    uint PrimID : SV_PrimitiveID;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;

    // Just pass data over to geometry shader.
    vout.CenterW = vin.PosW;
    vout.SizeW = vin.SizeW;

    return vout;
}

// We expand each point into a quad (4 vertices), so the maximum number of vertices
// we output per geometry shader invocation is 4.
[maxvertexcount(4)]
void GS(point VertexOut gin[1], uint primID : SV_PrimitiveID,
        inout TriangleStream<GeoOut> triStream)
{
    //
    // Compute the local coordinate system of the sprite relative to the world
    // space such that the billboard is aligned with the y-axis and faces the eye.
    //

    float3 up = float3(0.0f, 1.0f, 0.0f);
    float3 look = worldBuffer.EyePosW - gin[0].CenterW;
    look.y = 0.0f; // y-axis aligned, so project to xz-plane
    look = normalize(look);
    float3 right = cross(up, look);

    //
    // Compute triangle strip vertices (quad) in world space.
    //
    float halfWidth = 0.5f * gin[0].SizeW.x;
    float halfHeight = 0.5f * gin[0].SizeW.y;

    float4 v[4];
    v[0] = float4(gin[0].CenterW + halfWidth * right - halfHeight * up, 1.0f);
    v[1] = float4(gin[0].CenterW + halfWidth * right + halfHeight * up, 1.0f);
    v[2] = float4(gin[0].CenterW - halfWidth * right - halfHeight * up, 1.0f);
    v[3] = float4(gin[0].CenterW - halfWidth * right + halfHeight * up, 1.0f);

    //
    // Transform quad vertices to world space and output 
    // them as a triangle strip.
    //

    float2 texC[4] =
    {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, 0.0f)
    };

    GeoOut gout;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        gout.PosH = mul(v[i], worldBuffer.ViewProj);
        gout.PosW = v[i].xyz;
        gout.NormalW = look;
        gout.TexC = texC[i];
        gout.PrimID = primID;

        triStream.Append(gout);
    }

    triStream.RestartStrip();
}

float4 PS(GeoOut pin) : SV_Target
{
    MaterialData materialBuffer = materialData[objectBuffer.materialIndex];
    float3 uvw = float3(pin.TexC, pin.PrimID % 3);
    /*��������� ��� �������� ��� ���� ������� ������, �� ����� ������ ����� Texture2DArray
     *������� �������� � ��������� ��� � �������� �������. POG*/
    float4 diffuseAlbedo = texturesMaps[materialBuffer.DiffuseMapIndex].Sample(gsamAnisotropicWrap, uvw) *
        materialBuffer.DiffuseAlbedo;

#ifdef ALPHA_TEST
	// Discard pixel if texture alpha < 0.1.  We do this test as soon 
	// as possible in the shader so that we can potentially exit the
	// shader early, thereby skipping the rest of the shader code.
	clip(diffuseAlbedo.a - 0.1f);
#endif

    // Interpolating normal can unnormalize it, so renormalize it.
    pin.NormalW = normalize(pin.NormalW);

    // Vector from point being lit to eye. 
    float3 toEyeW = worldBuffer.EyePosW - pin.PosW;
    float distToEye = length(toEyeW);
    toEyeW /= distToEye; // normalize

    // Light terms.
    float4 ambient = worldBuffer.AmbientLight * diffuseAlbedo;

    const float shininess = 1.0f - materialBuffer.Roughness;
    Material mat = {diffuseAlbedo, materialBuffer.FresnelR0, shininess};
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(worldBuffer.Lights, mat, pin.PosW,
                                         pin.NormalW, toEyeW, shadowFactor);

    float4 litColor = ambient + directLight;


    // Common convention to take alpha from diffuse albedo.
    litColor.a = diffuseAlbedo.a;

    return litColor;
}
