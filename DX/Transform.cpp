#include "Transform.h"
namespace GameEngine
{
	using namespace DirectX::SimpleMath;

	Matrix Transform::CreateWorldMatrix() const
	{
		const Matrix mat = Matrix::CreateScale(scale.x, scale.y, scale.z) * Matrix::CreateFromYawPitchRoll(this->rot.x, this->rot.y, this->rot.z)
							* Matrix::CreateTranslation(this->pos.x, this->pos.y, this->pos.z);

		return mat;
	}

	Matrix Transform::GetWorldMatrix() const
	{
		if (Parent)
		{
			if (IsUseOnlyParentPosition)
			{
				return worldMatrix * Matrix::CreateScale(Parent->scale.x, Parent->scale.y, Parent->scale.z) * Matrix::
					CreateTranslation(Parent->pos.x, Parent->pos.y, Parent->pos.z);
			}
			return worldMatrix * Parent->GetWorldMatrix();
		}

		return worldMatrix;
	}

	Transform& Transform::GetParent() const
	{
		return *Parent;
	}

	void Transform::Init()
	{
		this->SetPosition(0.0f, 0.0f, 0.0f);
		this->SetRotation(0.0f, 0.0f, 0.0f);
		this->SetScale(1, 1, 1);
		this->Update();
	}

	void Transform::Update()
	{
		this->worldMatrix = CreateWorldMatrix();

		const Matrix vecRotationMatrix = Matrix::CreateFromYawPitchRoll(0.0f, this->rot.y, 0.0f);
		this->vec_forward = Vector3::Transform(Vector3::Forward, vecRotationMatrix);
		this->vec_backward = Vector3::Transform(Vector3::Backward, vecRotationMatrix);
		this->vec_left = Vector3::Transform(Vector3::Right, vecRotationMatrix);
		this->vec_right = Vector3::Transform(Vector3::Left, vecRotationMatrix);
	}

	void Transform::SetScale(float x, float y, float z)
	{
		this->scale = Vector3(x, y, z);
	}

	void Transform::SetParent(Transform* parent)
	{
		Parent = parent;
	}

	const Vector3& Transform::GetPositionVector() const
	{
		return this->pos;
	}


	const Vector3& Transform::GetRotationVector() const
	{
		return this->rot;
	}

	void Transform::SetScale(const Vector3& scl)
	{
		this->scale = scl;
	}


	void Transform::SetPosition(const Vector3& pos)
	{
		this->pos = pos;
	}

	void Transform::SetPosition(float x, float y, float z)
	{
		this->pos = Vector3(x, y, z);
	}

	void Transform::AdjustPosition(const Vector3& pos)
	{
		this->pos += pos;
	}

	void Transform::AdjustPosition(float x, float y, float z)
	{
		this->pos.x += x;
		this->pos.y += y;
		this->pos.z += z;
	}

	void Transform::SetRotation(const Vector3& rot)
	{
		this->rot = rot;
	}


	void Transform::SetRotation(float x, float y, float z)
	{
		this->rot = Vector3(x, y, z);
	}

	void Transform::AdjustRotation(const Vector3& rot)
	{
		this->rot += rot;
	}

	void Transform::AdjustRotation(float x, float y, float z)
	{
		this->rot.x += x;
		this->rot.y += y;
		this->rot.z += z;
	}

	void Transform::SetLookAtPos(Vector3 lookAtPos)
	{
		if (lookAtPos.x == this->pos.x && lookAtPos.y == this->pos.y && lookAtPos.z == this->pos.z)
			return;

		lookAtPos.x = this->pos.x - lookAtPos.x;
		lookAtPos.y = this->pos.y - lookAtPos.y;
		lookAtPos.z = this->pos.z - lookAtPos.z;

		float pitch = 0.0f;
		if (lookAtPos.y != 0.0f)
		{
			const float distance = sqrt(lookAtPos.x * lookAtPos.x + lookAtPos.z * lookAtPos.z);
			pitch = atan(lookAtPos.y / distance);
		}

		float yaw = 0.0f;
		if (lookAtPos.x != 0.0f)
		{
			yaw = atan(lookAtPos.x / lookAtPos.z);
		}
		if (lookAtPos.z > 0)
			yaw += DirectX::XM_PI;

		this->SetRotation(pitch, yaw, 0.0f);
	}

	const Vector3& Transform::GetForwardVector() const
	{
		return this->vec_forward;
	}

	const Vector3& Transform::GetRightVector() const
	{
		return this->vec_right;
	}

	const Vector3& Transform::GetBackwardVector() const
	{
		return this->vec_backward;
	}

	const Vector3& Transform::GetLeftVector() const
	{
		return this->vec_left;
	}


}