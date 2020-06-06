#include "Transform.h"
#include "GameObject.h"

UINT Transform::gConstantBufferIndex = 0;

Transform::Transform( ID3D12Device* device, Vector3 pos, Quaternion rot, Vector3 scale): Component(), position(pos), rotate(rot), scale(scale)
{	
	bufferIndex = gConstantBufferIndex++;
}

Transform::Transform( ID3D12Device* device) : Transform( device, Vector3::Zero, Quaternion::Identity, Vector3::One)
{
}


void Transform::Update()
{
	if (NumFramesDirty > 0)
	{
		 worldTranspose = world.Transpose();
		NumFramesDirty--;
	}
}

void Transform::Draw(ID3D12GraphicsCommandList* cmdList)
{
	
}

void Transform::SetParent(Transform* transform)
{
	Parent = std::unique_ptr<Transform>(transform);
}

Vector3 Transform::GetBackwardVector() const
{
	
	auto v = Vector3{ worldTranspose._13, worldTranspose._23, worldTranspose._33};
	v.Normalize();
	return v;
}

Vector3 Transform::GetForwardVector() const
{
	return GetBackwardVector() * -1;
}

Vector3 Transform::GetLeftVector() const
{
	auto v = Vector3{ worldTranspose._11, worldTranspose._21, worldTranspose._31};
	v.Normalize();
	return v;
}

Vector3 Transform::GetRightVector() const
{
	return GetLeftVector() * -1;
}

Vector3 Transform::GetUpVector() const
{
	auto v = Vector3{ worldTranspose._12, worldTranspose._22, worldTranspose._32};
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
	Matrix mat = Matrix::CreateTranslation(position) * Matrix::CreateFromQuaternion(rotate)	* Matrix::CreateScale(scale);

	if (Parent != nullptr)
	{
		mat *= Parent->CalculateWorldMatrix();
	}

	return mat;
}

void Transform::SetWorldMatrix(const Matrix& mat)
{
	world = mat;
	NumFramesDirty = globalCountFrameResources;
}

void Transform::SetPosition(const Vector3& pos)
{
	position = pos;
	world = CalculateWorldMatrix();
	NumFramesDirty = globalCountFrameResources;
}

void Transform::SetScale(const Vector3& s)
{
	scale = s;
	world = CalculateWorldMatrix();
	NumFramesDirty = globalCountFrameResources;
}

void Transform::SetEulerRotate(const Vector3& eulerAngl)
{
	eulerAngles = eulerAngl;	
	rotate = DirectX::XMQuaternionRotationRollPitchYaw((eulerAngles.x), (eulerAngles.y), (eulerAngles.z));
	world = CalculateWorldMatrix();
	NumFramesDirty = globalCountFrameResources;
}

void Transform::AdjustPosition(const Vector3& pos)
{
	SetPosition(position + pos);
}

void Transform::AdjustPosition(float x, float y, float z)
{
	SetPosition(position + Vector3(x, y, z));
}

void Transform::AdjustEulerRotation(const Vector3& rot)
{
	SetEulerRotate(eulerAngles + rot);
}

void Transform::AdjustEulerRotation(float roll, float pitch, float yaw)
{	
	SetEulerRotate(eulerAngles + Vector3(roll, pitch, yaw));
}

Vector3 Transform::GetPosition() const
{
	return position;
}

Vector3 Transform::GetScale() const
{
	return scale;
}

Quaternion Transform::GetQuaternionRotate() const
{
	return rotate;
}

Vector3 Transform::GetEulerAngels() const
{
	return eulerAngles;
}
