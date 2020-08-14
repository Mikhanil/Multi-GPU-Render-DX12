#pragma once
#include "Component.h"
#include "SimpleMath.h"

class GCommandList;
using namespace DirectX::SimpleMath;

class Camera : public Component
{
	void Draw(std::shared_ptr<GCommandList> cmdList) override
	{
	};
	void Update() override;

	void CreateProjection();

	Matrix view = Matrix::Identity;
	Matrix projection = Matrix::Identity;

	float fov = 60;
	float aspectRatio = 0;
	float nearZ = 0.1;
	float farZ = 10000;

	Vector3 focusPosition = Vector3::Zero;


	int NumFramesDirty = globalCountFrameResources;
public:

	const Vector3& GetFocusPosition() const
	{
		return focusPosition;
	}

	Camera(float aspect);;

	void SetAspectRatio(float aspect);

	void SetFov(float fov);

	float GetFov() const;

	const Matrix& GetViewMatrix() const;

	const Matrix& GetProjectionMatrix() const;
};
