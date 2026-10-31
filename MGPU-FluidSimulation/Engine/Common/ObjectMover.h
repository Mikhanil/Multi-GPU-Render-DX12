#pragma once
#include "Component.h"


using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;

class KeyboardDevice;

class ObjectMover :
    public Component
{
    KeyboardDevice* keyboard;

    void Update(const PEPEngine::Utils::GameTimer* gt) override;;

public:
    ObjectMover();;
};
