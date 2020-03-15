#include "Camera.h"

namespace GameEngine
{
	using namespace DirectX;
	using namespace DirectX::SimpleMath;
	
	Camera::Camera() = default;

	void Camera::SetProjectionValues(float fovDegrees, float aspectRatio, float nearZ, float farZ)
	{
		for (auto& component : components)
		{
			component->Init();
		}

		const float fovRadians = (fovDegrees / 360.0f) * XM_2PI;
		
		this->projectionMatrix =  Matrix::CreatePerspectiveFieldOfView(fovRadians, aspectRatio, nearZ, farZ);

		for (auto& component : components)
		{
			component->Update();
		}
	}

	const Matrix& Camera::GetViewMatrix() const
	{
		return this->viewMatrix;
	}

	const Matrix& Camera::GetProjectionMatrix() const
	{
		return this->projectionMatrix;
	}


	void Camera::Update() //Updates view matrix and also updates the movement vectors
	{
		for (auto& component : components)
		{
			component->Update();
		}

		const auto tr = GetTransform();

		const auto posv = new XMVECTOR;
		const auto scalev = new XMVECTOR;
		const auto rotv = new XMVECTOR;

		XMMatrixDecompose(scalev, rotv, posv, tr->GetWorldMatrix());

		
		//Calculate camera rotation matrix	
		Matrix camRotationMatrix = Matrix::CreateFromYawPitchRoll(tr->rot.x, tr->rot.y, tr->rot.z);
		//Calculate unit vector of cam target based off camera forward value transformed by cam rotation matrix
		Vector3 camTarget = Vector3::Transform(SimpleMath::Vector3::Forward, camRotationMatrix);
		//Adjust cam target to be offset by the camera's current position
		camTarget += *posv;
		//Calculate up direction based on current rotation
		Vector3 upDir = Vector3::Transform(SimpleMath::Vector3::Up, camRotationMatrix);
		//Rebuild view matrix
		this->viewMatrix = Matrix::CreateLookAt(*posv, camTarget, upDir);

		delete posv;
		delete scalev;
		delete rotv;
	}
}