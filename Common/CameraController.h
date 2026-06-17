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


    float xMouseSpeed = 10;//1000;
    float yMouseSpeed = 10;//700;

public:
    CameraController();

    void Update() override;
};
