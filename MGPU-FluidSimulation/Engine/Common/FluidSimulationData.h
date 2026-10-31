#pragma once

#include <SimpleMath.h>

using namespace DirectX::SimpleMath;

struct alignas(16) FluidSimulationData
{
	uint32_t numParticles = 10000;
	float gravity = -9.81f;
	float deltaTime;
	float simTime;
	// ---- 16 bytes
	float collisionDamping = 0.95f;
	float smoothingRadius = 0.2f;
	float targetDensity = 630.f;
	float pressureMultiplier = 288.f;
	// ---- 16 bytes
	float nearPressureMultiplier = 2.15f;
	float viscosityStrength = 0.001f;
	float _padding0;
	float _padding1;
	// ---- 16 bytes
	Vector3 boundsSize = Vector3(1.f, 1.f, 1.f);
	float _padding2;
	
	Matrix localToWorld = Matrix::Identity;
	Matrix worldToLocal = Matrix::Identity;

	Vector2 interactionInputPoint = Vector2::Zero;
	float interactionInputRadius = 1.f;
	float interactionInputStrength = 0.f;
};

struct alignas(16) SpikyKernels
{
	float SpikyPow2;
	float SpikyPow3;
	float SpikyPow2Grad;
	float SpikyPow3Grad;
};