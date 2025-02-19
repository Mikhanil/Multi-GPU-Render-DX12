#pragma once
#include "Component.h"


class Mousepad;
class KeyboardDevice;

using namespace DirectX::SimpleMath;

class CameraController :
    public Component
{
    KeyboardDevice* keyboard;
    Mousepad* mouse;


    float xMouseSpeed = 100;
    float yMouseSpeed = 70;

public:
    CameraController();

    void Update() override;
};
