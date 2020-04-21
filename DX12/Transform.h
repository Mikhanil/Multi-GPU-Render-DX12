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

	void SetRotate(const Quaternion& quat);

	void AdjustPosition(const Vector3& pos);

	void AdjustPosition(float x, float y, float z);

	[[nodiscard]]  Vector3 GetPosition() const;

	[[nodiscard]]  Vector3 GetScale() const;

	[[nodiscard]]  Quaternion GetRotate() const;

	Matrix TextureTransform = Matrix::CreateScale(Vector3::One);


	void Update() override;;
	void Draw(ID3D12GraphicsCommandList* cmdList) override;;

	void SetParent(Transform* transform);

private:

	[[nodiscard]] Matrix GetWorldMatrix() const;


	static UINT gConstantBufferIndex;

	std::unique_ptr<Transform> Parent = nullptr;

	UINT bufferIndex = -1;	
	int NumFramesDirty = globalCountFrameResources;
	ObjectConstants bufferConstant{};

	Vector3 position;
	Quaternion rotate;
	Vector3 scale;

	std::unique_ptr<ConstantBuffer<ObjectConstants>> objectWorldPositionBuffer = nullptr;
	
};


