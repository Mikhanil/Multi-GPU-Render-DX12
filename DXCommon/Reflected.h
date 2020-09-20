#pragma once
#include "Renderer.h"
#include "Transform.h"

using namespace DirectX::SimpleMath;

class Reflected : public Renderer
{
	Renderer* originRenderer;

public:
	Reflected(Renderer* originR);

	Plane mirrorPlane = {0.0f, 0.0f, 1.0f, 0.0f}; // xy plane

	void Update() override;;
	void Draw(std::shared_ptr<GCommandList> cmdList) override;
};
