#pragma once
#include "d3dUtil.h"

class GameObject;


class Component
{
public:

	GameObject* gameObject = nullptr;

	Component();

	virtual void Update() = 0;
	virtual void Draw(ID3D12GraphicsCommandList* cmdList) = 0;
};
