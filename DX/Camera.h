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

		/*Костыли велосепеды*/
		static Camera& Instance()
		{
			static Camera camera;
			return camera;
		}


		void Update() override;

	private:

		Matrix viewMatrix{};
		Matrix projectionMatrix{};

	};
}

