#pragma once
#include "Component.h"
#include "SimpleMath.h"
#include "DirectXBuffers.h"
using namespace DirectX::SimpleMath;

struct ObjectConstants
{
	DirectX::XMFLOAT4X4 World = Matrix::Identity;
	DirectX::XMFLOAT4X4 TextureTransform = Matrix::CreateScale(Vector3::One);
};


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


	void Update() override;;
	void Draw(ID3D12GraphicsCommandList* cmdList) override;;

	void SetParent(Transform* transform);

	Vector3 GetForwardVector()
	{		
		auto v = Vector3{ bufferConstant.World._13, bufferConstant.World._23, bufferConstant.World._33 };
		v.Normalize();
		return v;
	}

	Vector3 GetBackwardVector()
	{
		return GetForwardVector() * -1;
	}

	Vector3 GetRightVector()
	{
		auto v = Vector3{ bufferConstant.World._11, bufferConstant.World._21, bufferConstant.World._31 };
		v.Normalize();
		return v;
	}

	Vector3 GetLeftVector()
	{
		return GetRightVector() * -1;
	}

	Vector3 GetUpVector()
	{
		auto v = Vector3{ bufferConstant.World._12, bufferConstant.World._22, bufferConstant.World._32 };
		v.Normalize();
		return v;
	}

	Vector3 GetDownVector()
	{
		return GetUpVector() * -1;
	}


	
private:

	[[nodiscard]] Matrix GetWorldMatrix() const;


	static UINT gConstantBufferIndex;

	std::unique_ptr<Transform> Parent = nullptr;

	UINT bufferIndex = -1;	
	int NumFramesDirty = globalCountFrameResources;
	ObjectConstants bufferConstant{};

	Vector3 position = Vector3::Zero;

	Vector3 eulerAngles = Vector3::Zero;
	
	Quaternion rotate = Quaternion::Identity;
	Vector3 scale = Vector3::One;

	std::unique_ptr<ConstantBuffer<ObjectConstants>> objectWorldPositionBuffer = nullptr;
	
};


