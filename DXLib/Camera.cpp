#include "Camera.h"
#include "GameObject.h"

void Camera::Update()
{
	const auto transform = gameObject->GetTransform();

	/*std::wstring debug = L"Camera position " + std::to_wstring(transform->GetPosition().x) + L" : " + std::to_wstring(transform->GetPosition().y) + L" : " + std::to_wstring(transform->GetPosition().z) + L"\n"
		+ L"Camera rotation " + std::to_wstring(transform->GetEulerAngels().x) + L" : " + std::to_wstring(transform->GetEulerAngels().y) + L" : " + std::to_wstring(transform->GetEulerAngels().z) + L"\n";
	
	OutputDebugString(debug.c_str());*/

	Vector3 camtarget = XMVector3TransformCoord(Vector3::Forward,
	                                            Matrix::CreateFromQuaternion(transform->GetQuaternionRotate()));

	focusPosition = transform->GetForwardVector() + transform->GetPosition();

	view = XMMatrixLookAtLH(transform->GetPosition(), focusPosition, transform->GetUpVector());

	if (NumFramesDirty > 0)
	{
		CreateProjection();
		NumFramesDirty--;
	}
}

void Camera::CreateProjection()
{
	const float fovRadians = DirectX::XMConvertToRadians(fov);
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
