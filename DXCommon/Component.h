#pragma once
#include "d3d12.h"
#include "SimpleMath.h"
#include "MemoryAllocator.h"
#include <memory>

class GameObject;
class GCommandList;

class Component
{
public:

	GameObject* gameObject = nullptr;

	Component();

	virtual void Update() = 0;
	virtual void Draw(std::shared_ptr<GCommandList> cmdList) = 0;
};
