#include "Transform.h"
#include "GameObject.h"

UINT Transform::gConstantBufferIndex = 0;

Transform::Transform(Vector3 pos, Quaternion rot, Vector3 scale): Component(), localPosition(pos),
                                                                                        localRotate(rot), localScale(scale)
{
	bufferIndex = gConstantBufferIndex++;
}

Transform::Transform() : Transform(Vector3::Zero, Quaternion::Identity, Vector3::One)
{
}


void Transform::Update()
{

#if defined(_DEBUG)
	if (gameObject->GetName() == "MainCamera")
	{
		NumFramesDirty = globalCountFrameResources;
	}
#endif
	
	
	if (Parent == nullptr)
	{
		if (NumFramesDirty > 0)
		{
			worldTranspose = world.Transpose();
			NumFramesDirty--;
		}
	}
	else
	{
		if (Parent->IsDirty())
		{
			world = CalculateWorldMatrix();
			NumFramesDirty = globalCountFrameResources;
			worldTranspose = world.Transpose();
		}
		else if (NumFramesDirty > 0)
		{
			worldTranspose = world.Transpose();
			NumFramesDirty--;
		}
	}
}

void Transform::Draw(std::shared_ptr<GCommandList> cmdList)
{
}

void Transform::SetParent(Transform* transform)
{
	Parent = std::unique_ptr<Transform>(transform);
}

Vector3 Transform::GetBackwardVector() const
{
	auto v = Vector3{worldTranspose._13, worldTranspose._23, worldTranspose._33};
	v.Normalize();
	return v;
}

Vector3 Transform::GetForwardVector() const
{
	return GetBackwardVector() * -1;
}

Vector3 Transform::GetLeftVector() const
{
	auto v = Vector3{worldTranspose._11, worldTranspose._21, worldTranspose._31};
	v.Normalize();
	return v;
}

Vector3 Transform::GetRightVector() const
{
	return GetLeftVector() * -1;
}

Vector3 Transform::GetUpVector() const
{
	auto v = Vector3{worldTranspose._12, worldTranspose._22, worldTranspose._32};
	v.Normalize();
	return v;
}

Vector3 Transform::GetDownVector() const
{
	return GetUpVector() * -1;
}

Matrix Transform::GetWorldMatrix() const
{
	return world;
}


Matrix Transform::CalculateWorldMatrix() const
{
	Matrix result =  Matrix::CreateScale(localScale) * Matrix::CreateFromQuaternion(localRotate) *
		Matrix::CreateTranslation(localPosition);

	if (Parent != nullptr)
	{
		result = result * Parent->CalculateWorldMatrix() ;
	}

	return result;
}

void Transform::SetWorldMatrix(const Matrix& mat)
{
	world = mat;
	NumFramesDirty = globalCountFrameResources;
}

void Transform::SetPosition(const Vector3& pos)
{
	localPosition = pos;
	world = CalculateWorldMatrix();
	NumFramesDirty = globalCountFrameResources;
}

void Transform::SetScale(const Vector3& s)
{
	localScale = s;
	world = CalculateWorldMatrix();
	NumFramesDirty = globalCountFrameResources;
}

void Transform::SetEulerRotate(const Vector3& eulerAngl)
{
	localEulerAngles = eulerAngl;
	localRotate = Quaternion::CreateFromYawPitchRoll(DirectX::XMConvertToRadians(eulerAngl.y), DirectX::XMConvertToRadians(eulerAngl.x), DirectX::XMConvertToRadians(eulerAngl.z));	
	world = CalculateWorldMatrix();
	NumFramesDirty = globalCountFrameResources;
}

void Transform::SetRadianRotate(const Vector3& radianAngl)
{
	localEulerAngles = Vector3(DirectX::XMConvertToDegrees(radianAngl.x), DirectX::XMConvertToDegrees(radianAngl.y),
	                      DirectX::XMConvertToDegrees(radianAngl.z));
	
	localRotate = Quaternion::CreateFromYawPitchRoll(DirectX::XMConvertToRadians(radianAngl.y), DirectX::XMConvertToRadians(radianAngl.x), DirectX::XMConvertToRadians(radianAngl.z));

	world = CalculateWorldMatrix();
	NumFramesDirty = globalCountFrameResources;
}

void Transform::AdjustPosition(const Vector3& pos)
{
	SetPosition(localPosition + pos);
}

void Transform::AdjustPosition(float x, float y, float z)
{
	SetPosition(localPosition + Vector3(x, y, z));
}

void Transform::AdjustEulerRotation(const Vector3& rot)
{
	SetEulerRotate(localEulerAngles + rot);
}

void Transform::AdjustEulerRotation(float roll, float pitch, float yaw)
{
	SetEulerRotate(localEulerAngles + Vector3(roll, pitch, yaw));
}

Vector3 Transform::GetWorldPosition() const
{		
	return Vector3(world.m[3]);
}

Vector3 Transform::GetScale() const
{
	return localScale;
}

Quaternion Transform::GetQuaternionRotate() const
{
	return localRotate;
}

Vector3 Transform::GetEulerAngels() const
{
	return localEulerAngles;
}
