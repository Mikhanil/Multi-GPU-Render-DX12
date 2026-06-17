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
    float moveSpeed = 6.0f;

public:
    CameraController(float moveSpeed = 6.0f, float xMouseSens = 100.0f, float yMouseSens = 70.0f);

    void Update() override;
};
