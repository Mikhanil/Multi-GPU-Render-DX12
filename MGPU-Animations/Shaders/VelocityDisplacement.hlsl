// VelocityDisplacement.hlsl

struct RestVertex
{
    float3 PosL;
    float3 NormalL;
    float2 TexC;
    float3 TangentU;
    float3 BoneWeights;
    uint4  BoneIndices;
};

StructuredBuffer<RestVertex> g_RestVertices : register(t0, space0);
RWStructuredBuffer<float3> g_DisplacementOut : register(u0, space0);

cbuffer cbSkinned : register(b0)   // matches compute root signature slot 0
{
    float4x4 gBoneTransforms[96];
    float3   gBoneAngularVelocity[96];
    float3   gBoneLinearVelocity[96];
    float4   gBonePivot[96];
};

float3 RotateAroundAxis(float3 p, float3 axis, float angle)
{
    float cosA = cos(angle);
    float sinA = sin(angle);
    return p * cosA + cross(axis, p) * sinA + axis * dot(axis, p) * (1.0 - cosA);
}

float3 ComputeDisplacement(uint vertexIndex)
{
    RestVertex vin = g_RestVertices[vertexIndex];
    float weights[4] = { vin.BoneWeights.x, vin.BoneWeights.y, vin.BoneWeights.z,
                         1.0f - vin.BoneWeights.x - vin.BoneWeights.y - vin.BoneWeights.z };
    uint4 indices = vin.BoneIndices;

    //float3 posW = float3(0,0,0);
    float3 dispAngular = float3(0,0,0);
    float3 dispLinear = float3(0,0,0);

    [unroll]
    for (int j = 0; j < 4; ++j)
    {
        float w = weights[j];
        if (w == 0.0f) continue;
        uint boneIndex = indices[j];

        float3 pivot = gBonePivot[boneIndex].xyz;
        float3 r = vin.PosL - pivot;

        float3 omega = gBoneAngularVelocity[boneIndex];
        float omegaMag = length(omega);
        float3 axis = omega / (omegaMag + 1e-10f);

        float3 rPerp = r - dot(r, axis) * axis;
        float dist = length(rPerp);
        float k_floppy = 0.05;
        float angle = -k_floppy * omegaMag * dist;

        float3 rotatedPerp = RotateAroundAxis(rPerp, axis, angle);
        float3 angularOffsetModel = rotatedPerp - rPerp;

        float3x3 boneRot = (float3x3)gBoneTransforms[boneIndex];
        float3 angularOffsetWorld = mul(angularOffsetModel, boneRot);

        float3 linearOffsetWorld = gBoneLinearVelocity[boneIndex] * -k_floppy;

        float4 transformedPos = mul(float4(vin.PosL, 1.0f), gBoneTransforms[boneIndex]);
        //posW += w * transformedPos.xyz;

        dispAngular += w * angularOffsetWorld;
        dispLinear  += w * linearOffsetWorld;
    }
    return dispAngular + dispLinear;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dtID : SV_DispatchThreadID)
{
    uint idx = dtID.x;
    g_DisplacementOut[idx] = ComputeDisplacement(idx);
}