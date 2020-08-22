#pragma once
#include "Component.h"
#include "Material.h"

class GCommandList;

class Renderer : public Component
{
public:

	Renderer(): Component() {} ;

	Material* material = nullptr;

	void Update() override = 0;
	virtual void Draw(std::shared_ptr<GCommandList> cmdList) = 0;
};
