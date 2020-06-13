#pragma once
#include <SimpleMath.h>

#define MaxLights 16

using namespace DirectX::SimpleMath;

struct Vertex
{
	Vertex() {}
	Vertex(
		Vector3& p,
		Vector3& n,
		Vector3& t,
		Vector2& uv) :
		Position(p),
		Normal(n),
		TexCord(uv),
		TangentU(t) {}
	Vertex(
		float px, float py, float pz,
		float nx, float ny, float nz,
		float tx, float ty, float tz,
		float u, float v) :
		Position(px, py, pz),
		Normal(nx, ny, nz),
		TangentU(tx, ty, tz),
		TexCord(u, v) {}

	Vector3 Position;
	Vector3 Normal;
	Vector2 TexCord;
	Vector3 TangentU;
};

enum LightType
{
	Directional,
	Point,
	Spot
};

struct LightData
{
	Vector3 Strength = Vector3{0.5f, 0.5f, 0.5f};
	float FalloffStart = 1.0f; // point/spot light only
	Vector3 Direction = Vector3{0.0f, -1.0f, 0.0f}; // directional/spot light only
	float FalloffEnd = 10.0f; // point/spot light only
	Vector3 Position = Vector3{0.0f, 0.0f, 0.0f}; // point/spot light only
	float SpotPower = 64.0f; // spot light only
};

struct PassConstants
{
	Matrix View = DirectX::SimpleMath::Matrix::Identity;
	Matrix InvView = DirectX::SimpleMath::Matrix::Identity;
	Matrix Proj = DirectX::SimpleMath::Matrix::Identity;
	Matrix InvProj = DirectX::SimpleMath::Matrix::Identity;
	Matrix ViewProj = DirectX::SimpleMath::Matrix::Identity;
	Matrix InvViewProj = DirectX::SimpleMath::Matrix::Identity;
	Matrix ViewProjTex = DirectX::SimpleMath::Matrix::Identity;
	Matrix ShadowTransform = DirectX::SimpleMath::Matrix::Identity;
	Vector3 EyePosW = Vector3{0.0f, 0.0f, 0.0f};
	float tempFloat = 0.0f;
	Vector2 RenderTargetSize = Vector2{0.0f, 0.0f};
	Vector2 InvRenderTargetSize = Vector2{0.0f, 0.0f};
	float NearZ = 0.0f;
	float FarZ = 0.0f;
	float TotalTime = 0.0f;
	float DeltaTime = 0.0f;
	Vector4 AmbientLight = Vector4{0.0f, 0.0f, 0.0f, 1.0f};


	Vector4 FogColor = Vector4{0.7f, 0.7f, 0.7f, 1.0f};
	float gFogStart = 5.0f;
	float gFogRange = 150.0f;
	Vector2 cbPerObjectPad2;

	LightData Lights[MaxLights];
};


struct ObjectConstants
{
	Matrix World = DirectX::SimpleMath::Matrix::Identity;
	Matrix TextureTransform = DirectX::SimpleMath::Matrix::CreateScale(DirectX::SimpleMath::Vector3::One);
	UINT materialIndex;
	UINT gObjPad0;
	UINT gObjPad1;
	UINT gObjPad2;
};

struct SsaoConstants
{
	Matrix Proj;
	Matrix InvProj;
	Matrix ProjTex;
	Vector4   OffsetVectors[14];

	// For SsaoBlur.hlsl
	Vector4 BlurWeights[3];

	Vector2 InvRenderTargetSize = { 0.0f, 0.0f };

	// Coordinates given in view space.
	float OcclusionRadius = 0.5f;
	float OcclusionFadeStart = 0.2f;
	float OcclusionFadeEnd = 2.0f;
	float SurfaceEpsilon = 0.05f;
};

struct MaterialConstants
{
	Vector4 DiffuseAlbedo = Vector4{1.0f, 1.0f, 1.0f, 1.0f};
	Vector3 FresnelR0 = Vector3{0.01f, 0.01f, 0.01f};
	float Roughness = 0.25f;

	// Used in texture mapping.
	Matrix MaterialTransform = DirectX::SimpleMath::Matrix::Identity;
	UINT DiffuseMapIndex;
	UINT NormalMapIndex;
	UINT MatPad1;
	UINT MatPad2;
};


class StandardShaderSlot
{
public:
	enum Register
	{		
		ObjectData,
		CameraData,
		MaterialData,
		SkyMap,
		ShadowMap,
		SsaoMap,
		TexturesMap,
		Count
	};
};