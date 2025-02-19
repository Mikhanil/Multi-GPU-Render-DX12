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

    void Update() override;;

public:
    ObjectMover();;
};
