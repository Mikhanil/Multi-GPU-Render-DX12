#pragma once
#include "d3d12.h"
#include "SimpleMath.h"
#include "MemoryAllocator.h"
#include <memory>

#include "GCommandList.h"

namespace PEPEngine::Utils
{
    class GameTimer;
}

class GameObject;

class Component
{
public:
    GameObject* gameObject = nullptr;

    Component();

    virtual void Update(const PEPEngine::Utils::GameTimer* gt = nullptr) = 0;
};
