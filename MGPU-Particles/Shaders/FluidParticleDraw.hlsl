#define RED   float4(1, 0, 0, 1)
#define GREEN float4(0, 1, 0, 1)
#define BLUE  float4(0, 0, 1, 1)
#define MAX_SPEED 20.0f

static const float2 quadVertices[] = {
	float2(-0.5, -0.5),
	float2( 0.5, -0.5),
	float2( 0.5,  0.5),
	float2(-0.5,  0.5),
};

cbuffer Transform : register(b0)
{
	float4x4 ViewProj;

	float3 billboardUp;
	float _padding0;

	float3 billboardRight;
	float size;
}

StructuredBuffer<float3> ParticlePositions : register(t0);
StructuredBuffer<float3> ParticleVelocities: register(t1);

struct VSOut
{
	float4 Position : SV_Position;
	uint InstanceId;
};

VSOut VS(uint VertexId : SV_VertexID, uint InstanceId : SV_InstanceID)
{
	VSOut res;

	float2 quadPos = quadVertices[VertexId];
	float3 center = ParticlePositions[InstanceId];
	float3 offset = billboardRight * (quadPos.x * size)
					+ billboardUp * (quadPos.y * size);

	float3 worldPos = center + offset;
	res.Position = mul(float4(worldPos, 1), ViewProj);
	res.InstanceId = InstanceId;

	return res;
}

float4 PS(VSOut input) : SV_Target0
{
	float speed = length(ParticleVelocities[input.InstanceId]);
	return saturate(lerp(BLUE, RED, speed / MAX_SPEED));
}
