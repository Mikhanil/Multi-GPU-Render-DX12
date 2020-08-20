#pragma once
#include "Component.h"
#include "Material.h"

class GCommandList;

class Renderer : public Component
{
protected:
	ObjectConstants bufferConstant{};
	std::unique_ptr<ConstantBuffer<ObjectConstants>> objectConstantBuffer = nullptr;
	void SendDataToConstantBuffer();

public:

	Renderer();


	Material* material = nullptr;
	


	void Update() override;
	virtual void Draw(std::shared_ptr<GCommandList> cmdList) = 0;
};
