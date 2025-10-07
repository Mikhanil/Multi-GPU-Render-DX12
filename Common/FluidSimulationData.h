#pragma once

#include <SimpleMath.h>

using namespace DirectX::SimpleMath;

struct alignas(16) FluidSimulationData
{
	uint32_t numParticles;
	float gravity;
	float deltaTime;
	float simTime;
	// ---- 16 bytes
	float collisionDamping;
	float smoothingRadius;
	float targetDensity;
	float pressureMultiplier;
	// ---- 16 bytes
	float nearPressureMultiplier;
	float viscosityStrength;
	float edgeForce;
	float edgeForceDst;
	// ---- 16 bytes
	Vector3 boundsSize;
	float _padding0;
	
	Matrix localToWorld;
	Matrix worldToLocal;

	Vector2 interactionInputPoint;
	float interactionInputRadius;
	float interactionInputStrength;
	
	uint32_t densityMapSize[3];
};