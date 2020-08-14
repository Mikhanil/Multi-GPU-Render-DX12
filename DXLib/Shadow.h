#pragma once
#include "Renderer.h"
#include "Transform.h"
#include "Light.h"

using namespace DirectX::SimpleMath;

class Shadow : public Renderer
{
	void Update() override;

	void Draw(std::shared_ptr<GCommandList> cmdList) override
	{
		Renderer::Draw(cmdList);
	};

	Transform* targetTransform;
	Light* mainLight;

public:

	Plane shadowPlane = {0.0f, 1.0f, 0.0f, 0.0f}; // xz plane

	Shadow(Transform* targetTr, Light* light);
};
