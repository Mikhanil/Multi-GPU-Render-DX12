#pragma once
#include "Component.h"
#include "d3dx12.h"
#include "d3d.h"
#include "SimpleMath.h"

namespace GameEngine
{
	using namespace DirectX::SimpleMath;
	
	class Transform :
		public Component
	{
		Vector3 vec_forward;
		Vector3 vec_left;
		Vector3 vec_right;
		Vector3 vec_backward;

		Matrix worldMatrix = Matrix::Identity;

		Transform* Parent = nullptr;

		Matrix CreateWorldMatrix() const;

	public:
		Vector3 pos;
		Vector3 rot;
		Vector3 scale;

		bool IsUseOnlyParentPosition;

		Matrix GetWorldMatrix() const;

		Transform& GetParent() const;


		void Init() override;;
		void Update() override;;


		const Vector3& GetPositionVector() const;
		const Vector3& GetRotationVector() const;

		void SetScale(const Vector3& scl);;
		void SetScale(float x, float y, float z);;

		void SetParent(Transform* parent);

		void SetPosition(const Vector3& pos);
		void SetPosition(float x, float y, float z);
		void AdjustPosition(const Vector3& pos);
		void AdjustPosition(float x, float y, float z);
		void SetRotation(const Vector3& rot);
		void SetRotation(float x, float y, float z);
		void AdjustRotation(const Vector3& rot);
		void AdjustRotation(float x, float y, float z);
		
		void SetLookAtPos(Vector3 lookAtPos);

		const Vector3& GetForwardVector() const;
		const Vector3& GetRightVector() const;
		const Vector3& GetBackwardVector() const;
		const Vector3& GetLeftVector() const;
	};

}
