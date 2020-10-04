#pragma once
#include <SimpleMath.h>

#define MaxLights 16

using namespace DirectX::SimpleMath;

static const DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
static const DXGI_FORMAT AmbientMapFormat = DXGI_FORMAT_R16_UNORM;
static const DXGI_FORMAT NormalMapFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
static const DXGI_FORMAT DepthMapFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

struct Vertex
{
	Vertex()
	{
	}

	Vertex(
		Vector3& p,
		Vector3& n,
		Vector3& t,
		Vector2& uv) :
		Position(p),
		Normal(n),
		TexCord(uv),
		TangentU(t)
	{
	}

	Vertex(
		float px, float py, float pz,
		float nx, float ny, float nz,
		float tx, float ty, float tz,
		float u, float v) :
		Position(px, py, pz),
		Normal(nx, ny, nz),
		TexCord(u, v),
		TangentU(tx, ty, tz)
	{
	}

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
	Matrix View = Matrix::Identity;
	Matrix InvView = Matrix::Identity;
	Matrix Proj = Matrix::Identity;
	Matrix InvProj = Matrix::Identity;
	Matrix ViewProj = Matrix::Identity;
	Matrix InvViewProj = Matrix::Identity;
	Matrix ViewProjTex = Matrix::Identity;
	Matrix ShadowTransform = Matrix::Identity;
	Vector3 EyePosW = Vector3{0.0f, 0.0f, 0.0f};
	float debugMap = 0.0f;
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
	Matrix World = Matrix::Identity;
	Matrix TextureTransform = Matrix::CreateScale(Vector3::One);
	UINT MaterialIndex = 0;
	UINT gObjPad0;
	UINT gObjPad1;
	UINT gObjPad2;
};

struct SsaoConstants
{
	Matrix Proj;
	Matrix InvProj;
	Matrix ProjTex;
	Vector4 OffsetVectors[14];

	// For SsaoBlur.hlsl
	Vector4 BlurWeights[3];

	Vector2 InvRenderTargetSize = {0.0f, 0.0f};

	// Coordinates given in view space.
	float OcclusionRadius = 0.5f;
	float OcclusionFadeStart = 0.2f;
	float OcclusionFadeEnd = 2.0f;
	float SurfaceEpsilon = 0.05f;
};

struct alignas(sizeof(Vector4)) MaterialConstants
{
	Vector4 DiffuseAlbedo = Vector4{1.0f, 1.0f, 1.0f, 1.0f};
	Vector3 FresnelR0 = Vector3{0.01f, 0.01f, 0.01f};
	float Roughness = 0.25f;

	// Used in texture mapping.
	Matrix MaterialTransform = Matrix::Identity;
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
		AmbientMap,
		TexturesMap,
		Count
	};
};
