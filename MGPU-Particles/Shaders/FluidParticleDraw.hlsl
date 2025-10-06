cbuffer SimulationConstants : register(b0)
{  
	uint numParticles;
	float gravity;
	float deltaTime;
	float simTime;
	float collisionDamping;
	float smoothingRadius;
	float targetDensity;
	float pressureMultiplier;
	float nearPressureMultiplier;
	float viscosityStrength;
	float edgeForce;
	float edgeForceDst;
	float3 boundsSize;
	float _padding0;

	float4x4 localToWorld;
	float4x4 worldToLocal;

	float2 interactionInputPoint;
	float interactionInputStrength;
	float interactionInputRadius;

	// Volume texture settings
	uint3 densityMapSize;
};

RWStructuredBuffer<float3> ParticlePositions : register(t0);
RWStructuredBuffer<float3> ParticleVelocities: register(t1);

