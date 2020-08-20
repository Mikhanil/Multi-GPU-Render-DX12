#pragma once
#include "Transform.h"
#include "Light.h"
#include "ModelRenderer.h"

using namespace DirectX::SimpleMath;

class Shadow : public ModelRenderer
{
	void Update() override;

	void Draw(std::shared_ptr<GCommandList> cmdList) override;;

	Transform* targetTransform;
	Light* mainLight;

public:

	Plane shadowPlane = {0.0f, 1.0f, 0.0f, 0.0f}; // xz plane

	Shadow(Transform* targetTr, Light* light);
};
