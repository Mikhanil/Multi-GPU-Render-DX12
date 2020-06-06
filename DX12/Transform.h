#pragma once
#include "Component.h"
#include "SimpleMath.h"
#include "DirectXBuffers.h"
using namespace DirectX::SimpleMath;


class Transform :	public Component
{

public:
	Transform(ID3D12Device* device, Vector3 pos, Quaternion rot , Vector3 scale);

	Transform(ID3D12Device* device);

	void SetPosition(const Vector3& pos);

	void SetScale(const Vector3& s);

	void SetEulerRotate(const Vector3& eulerAngl);

	void AdjustPosition(const Vector3& pos);

	void AdjustPosition(float x, float y, float z);

	void AdjustEulerRotation(const Vector3& eulerAngl);

	void AdjustEulerRotation(float roll, float pitch, float yaw);

	[[nodiscard]]  Vector3 GetPosition() const;

	[[nodiscard]]  Vector3 GetScale() const;

	[[nodiscard]] Quaternion GetQuaternionRotate() const;
	[[nodiscard]] Vector3 GetEulerAngels() const;;

	Matrix TextureTransform = Matrix::CreateScale(Vector3::One);
	Matrix worldTranspose;

	bool IsDirty()
	{
		return NumFramesDirty > 0;
	}

	void Update() override;;
	void Draw(ID3D12GraphicsCommandList* cmdList) override;;

	void SetParent(Transform* transform);

	Vector3 GetForwardVector() const;

	Vector3 GetBackwardVector() const;

	Vector3 GetRightVector() const;

	Vector3 GetLeftVector() const;

	Vector3 GetUpVector() const;

	Vector3 GetDownVector() const;

	Matrix GetWorldMatrix() const;

	void SetWorldMatrix(const Matrix& mat);

private:

	Matrix world = Matrix::Identity;


	Matrix CalculateWorldMatrix() const;

	static UINT gConstantBufferIndex;

	std::unique_ptr<Transform> Parent = nullptr;

	UINT bufferIndex = -1;	
	int NumFramesDirty = globalCountFrameResources;

	Vector3 position = Vector3::Zero;

	Vector3 eulerAngles = Vector3::Zero;
	
	Quaternion rotate = Quaternion::Identity;
	Vector3 scale = Vector3::One;

	
	
};


