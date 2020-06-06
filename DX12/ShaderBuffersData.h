#pragma once
#include <SimpleMath.h>

#define MaxLights 16

enum LightType
{
	Directional,
	Point,
	Spot
};

struct LightData
{
	DirectX::XMFLOAT3 Strength = {0.5f, 0.5f, 0.5f};
	float FalloffStart = 1.0f; // point/spot light only
	DirectX::XMFLOAT3 Direction = {0.0f, -1.0f, 0.0f}; // directional/spot light only
	float FalloffEnd = 10.0f; // point/spot light only
	DirectX::XMFLOAT3 Position = {0.0f, 0.0f, 0.0f}; // point/spot light only
	float SpotPower = 64.0f; // spot light only
};

struct PassConstants
{
	DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 InvView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT3 EyePosW = {0.0f, 0.0f, 0.0f};
	float tempFloat = 0.0f;
	DirectX::XMFLOAT2 RenderTargetSize = {0.0f, 0.0f};
	DirectX::XMFLOAT2 InvRenderTargetSize = {0.0f, 0.0f};
	float NearZ = 0.0f;
	float FarZ = 0.0f;
	float TotalTime = 0.0f;
	float DeltaTime = 0.0f;
	DirectX::XMFLOAT4 AmbientLight = {0.0f, 0.0f, 0.0f, 1.0f};


	DirectX::XMFLOAT4 FogColor = {0.7f, 0.7f, 0.7f, 1.0f};
	float gFogStart = 5.0f;
	float gFogRange = 150.0f;
	DirectX::XMFLOAT2 cbPerObjectPad2;

	LightData Lights[MaxLights];
};


struct ObjectConstants
{
	DirectX::XMFLOAT4X4 World = DirectX::SimpleMath::Matrix::Identity;
	DirectX::XMFLOAT4X4 TextureTransform = DirectX::SimpleMath::Matrix::CreateScale(DirectX::SimpleMath::Vector3::One);
	UINT materialIndex;
	UINT gObjPad0;
	UINT gObjPad1;
	UINT gObjPad2;
};

struct MaterialConstants
{
	DirectX::XMFLOAT4 DiffuseAlbedo = {1.0f, 1.0f, 1.0f, 1.0f};
	DirectX::XMFLOAT3 FresnelR0 = {0.01f, 0.01f, 0.01f};
	float Roughness = 0.25f;

	// Used in texture mapping.
	DirectX::XMFLOAT4X4 MaterialTransform = MathHelper::Identity4x4();
	UINT DiffuseMapIndex;
	UINT MatPad0;
	UINT MatPad1;
	UINT MatPad2;
};


class StandardShaderSlot
{
public:
	enum Register
	{
		DiffuseTexture,
		ObjectData,
		CameraData,
		MaterialData,
		Count
	};
};