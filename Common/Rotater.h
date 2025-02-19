#pragma once
#include "Component.h"
#include "d3dApp.h"

using namespace DirectX;
using namespace SimpleMath;

class Rotater :
    public Component
{
public:
    Rotater(const float speed = 1): speed(speed)
    {
    }

private:
    void Update() override;

    const float time = 2;
    float currentTime = 0;
    bool invers = false;


    float speed;
};
