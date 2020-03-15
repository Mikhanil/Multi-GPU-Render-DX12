#pragma once
#include "GameObject.h"

namespace  GameEngine
{
	class Camera : public GameObject
	{
	public:
		Camera();
		void SetProjectionValues(float fovDegrees, float aspectRatio, float nearZ, float farZ);

		const Matrix& GetViewMatrix() const;
		const Matrix& GetProjectionMatrix() const;



		void Update() override;

	private:

		Matrix viewMatrix{};
		Matrix projectionMatrix{};

	};
}

