#include "Camera.h"
#include "GameObject.h"

void Camera::Update()
{
	const auto transform = gameObject->GetTransform();

	Vector3 camtarget = DirectX::XMVector3TransformCoord(Vector3::Forward, Matrix::CreateFromQuaternion(transform->GetRotate()));
	
	view =  DirectX::XMMatrixLookAtLH(transform->GetPosition(), camtarget += transform->GetPosition(), Vector3::Up);

	if (NumFramesDirty > 0)
	{
		CreateProjection(fov, aspectRatio, nearZ, farZ);
		NumFramesDirty--;
	}
}

void Camera::CreateProjection(float fovDegrees, float aspectRatio, float nearZ, float farZ)
{
	const float fovRadians = DirectX::XMConvertToRadians(fovDegrees);  //(fovDegrees / 360.0f)* DirectX::XM_2PI;
	projection = DirectX::XMMatrixPerspectiveFovLH(fovRadians, aspectRatio, nearZ, farZ);
}

Camera::Camera(float aspect): aspectRatio(aspect)
{
}

void Camera::SetAspectRatio(float aspect)
{
	aspectRatio = aspect;
	NumFramesDirty = globalCountFrameResources;
}

void Camera::SetFov(float fov)
{
	this->fov = fov;
	NumFramesDirty = globalCountFrameResources;
}

float Camera::GetFov() const
{
	return fov;
}

const Matrix& Camera::GetViewMatrix() const
{
	return this->view;
}

const Matrix& Camera::GetProjectionMatrix() const
{
	return this->projection;
}
