#pragma once
#include "Component.h"
#include "SimpleMath.h"


using namespace DirectX::SimpleMath;

class Camera :	public Component
{

	void Draw(ID3D12GraphicsCommandList* cmdList) override {};
	void Update() override;

	void CreateProjection(float fovDegrees, float aspectRatio, float nearZ, float farZ);

	Matrix view = Matrix::Identity;
	Matrix projection = Matrix::Identity;
	
	float fov = 60;
	float aspectRatio = 0;
	float nearZ = 0.1;
	float farZ = 10000;
	
	int NumFramesDirty = globalCountFrameResources;
public:

	Camera(float aspect);;

	void SetAspectRatio(float aspect);

	void SetFov(float fov);

	float GetFov() const;

	const Matrix& GetViewMatrix() const;

	const Matrix& GetProjectionMatrix() const;
};

