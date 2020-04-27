#include "Transform.h"
#include "GameObject.h"

UINT Transform::gConstantBufferIndex = 0;

Transform::Transform( ID3D12Device* device, Vector3 pos, Quaternion rot, Vector3 scale): Component(), position(pos), rotate(rot), scale(scale)
{
	objectWorldPositionBuffer = std::make_unique<ConstantBuffer<ObjectConstants>>(device, 1);
	bufferIndex = gConstantBufferIndex++;
}

Transform::Transform( ID3D12Device* device) : Transform( device, Vector3::Zero, Quaternion::Identity, Vector3::One)
{
}


void Transform::Update()
{
	if (NumFramesDirty > 0)
	{
		bufferConstant.TextureTransform = TextureTransform.Transpose();
		bufferConstant.World = GetWorldMatrix().Transpose();
		objectWorldPositionBuffer->CopyData(0, bufferConstant);
		NumFramesDirty--;
	}
}

void Transform::Draw(ID3D12GraphicsCommandList* cmdList)
{
	cmdList->SetGraphicsRootConstantBufferView(1, objectWorldPositionBuffer->Resource()->GetGPUVirtualAddress());
}

void Transform::SetParent(Transform* transform)
{
	Parent = std::unique_ptr<Transform>(transform);
}

Matrix Transform::GetWorldMatrix() const
{
	Matrix mat = Matrix::CreateTranslation(position) * Matrix::CreateFromQuaternion(rotate)	* Matrix::CreateScale(scale);

	if (Parent != nullptr)
	{
		mat *= Parent->GetWorldMatrix();
	}

	return mat;
}

void Transform::SetPosition(const Vector3& pos)
{
	position = pos;
	NumFramesDirty = globalCountFrameResources;
}

void Transform::SetScale(const Vector3& s)
{
	scale = s;
	NumFramesDirty = globalCountFrameResources;
}

void Transform::SetEulerRotate(const Vector3& eulerAngl)
{
	eulerAngles = eulerAngl;	
	rotate = DirectX::XMQuaternionRotationRollPitchYaw((eulerAngles.x), (eulerAngles.y), (eulerAngles.z));
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
