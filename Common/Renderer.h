#pragma once
#include "Component.h"
#include "Material.h"


class Renderer : public Component
{
public:

	Renderer(): Component() {} ;

	void Update() override = 0;
	void Draw(std::shared_ptr<GCommandList> cmdList) override = 0;
};

