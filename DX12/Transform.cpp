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
	// Only update the cbuffer data if the constants have changed.  
	// This needs to be tracked per frame resource.
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
	Matrix mat = Matrix::CreateTranslation(position) * Matrix::CreateFromQuaternion(rotate) * Matrix::
		CreateScale(scale);

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

void Transform::SetRotate(const Quaternion& quat)
{
	rotate = quat;
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

Vector3 Transform::GetPosition() const
{
	return position;
}

Vector3 Transform::GetScale() const
{
	return scale;
}

Quaternion Transform::GetRotate() const
{
	return rotate;
}
