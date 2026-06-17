#pragma once

#include "SimpleMath.h"

using namespace DirectX::SimpleMath;

struct alignas(16) AtmosphereConstants
{
	DirectX::XMFLOAT3 solar_irradiance;
	float sun_angular_radius;

	DirectX::XMFLOAT3 absorption_extinction;
	float mu_s_min;

	DirectX::XMFLOAT3 rayleigh_scattering;
	float mie_phase_function_g;

	DirectX::XMFLOAT3 mie_scattering;
	float bottom_radius;

	DirectX::XMFLOAT3 mie_extinction;
	float top_radius;

	DirectX::XMFLOAT3 mie_absorption;
	float pad00;

	DirectX::XMFLOAT3 ground_albedo;
	float pad0;

	float rayleigh_density[12];
	float mie_density[12];
	float absorption_density[12];

	DirectX::XMFLOAT3 SKY_SPECTRAL_RADIANCE_TO_LUMINANCE;
	float pad3;
	DirectX::XMFLOAT3 SUN_SPECTRAL_RADIANCE_TO_LUMINANCE;
	float pad4;

	DirectX::XMFLOAT4X4 gSkyViewProjMat;
	DirectX::XMFLOAT4X4 gSkyInvViewProjMat;
	DirectX::XMFLOAT4X4 gSkyInvProjMat;
	DirectX::XMFLOAT4X4 gSkyInvViewMat;
	DirectX::XMFLOAT4X4 gShadowmapViewProjMat;

	DirectX::XMFLOAT3 camera;
	float pad5;
	DirectX::XMFLOAT3 sun_direction;
	float pad6;
	DirectX::XMFLOAT3 view_ray;
	float pad7;

	float MultipleScatteringFactor;
	float pad8;
	float pad9;
	float pad10;
};

struct AtmosphereCommonConstants
{
	Matrix viewProjMat;

	Vector4 color;

	Vector3 sunIlluminance;
	int scatteringMaxPathDepth;

	float frameTimeSec;
	float timeSec;
	unsigned int mouseLastDownPos[2];

	unsigned int frameId;
	unsigned int terrainResolution;
	float rayMarchMinMaxSPP[2];

	unsigned int gameResolution[2];
	unsigned int transmittanceLutResolution[2];

	unsigned int multiScatLutResolution[2];
	unsigned int skyViewLutResolution[2];

	unsigned int aerialPerpspectiveLutResolution[3];
	float screenshotCaptureActive;

	unsigned int rayMarchingResolution[2];
	float pad[2];
	
	Vector3 terrainPosDelta;
	float pad2;
};
