#pragma once
#include "Component.h"
#include "SimpleMath.h"
#include "DirectXBuffers.h"
using namespace DirectX::SimpleMath;


class Transform : public Component
{
public:
	Transform(ID3D12Device* device, Vector3 pos, Quaternion rot, Vector3 scale);

	Transform(ID3D12Device* device);

	void SetPosition(const Vector3& pos);

	void SetScale(const Vector3& s);

	void SetEulerRotate(const Vector3& eulerAngl);
	void SetRadianRotate(const Vector3& radianAngl);

	void AdjustPosition(const Vector3& pos);

	void AdjustPosition(float x, float y, float z);

	void AdjustEulerRotation(const Vector3& eulerAngl);

	void AdjustEulerRotation(float roll, float pitch, float yaw);

	[[nodiscard]] Vector3 GetWorldPosition() const;

	[[nodiscard]] Vector3 GetScale() const;

	[[nodiscard]] Quaternion GetQuaternionRotate() const;
	[[nodiscard]] Vector3 GetEulerAngels() const;;

	Matrix TextureTransform = Matrix::CreateScale(Vector3::One);
	Matrix worldTranspose;

	bool IsDirty()
	{
		return NumFramesDirty > 0;
	}

	void Update() override;;
	void Draw(std::shared_ptr<GCommandList> cmdList) override;;

	void SetParent(Transform* transform);

	Vector3 GetForwardVector() const;

	Vector3 GetBackwardVector() const;

	Vector3 GetRightVector() const;

	Vector3 GetLeftVector() const;

	Vector3 GetUpVector() const;

	Vector3 GetDownVector() const;

	Matrix GetWorldMatrix() const;
	Matrix MakeLocalToParent() const;

	void SetWorldMatrix(const Matrix& mat);

private:

	Matrix world = Matrix::Identity;


	Matrix MakeParentToLocal() const;
	Matrix CalculateWorldMatrix() const;

	static UINT gConstantBufferIndex;

	std::unique_ptr<Transform> Parent = nullptr;

	UINT bufferIndex = -1;
	int NumFramesDirty = globalCountFrameResources;

	Vector3 localPosition = Vector3::Zero;

	Vector3 localEulerAngles = Vector3::Zero;

	Quaternion localRotate = Quaternion::Identity;
	Vector3 localScale = Vector3::One;
};
