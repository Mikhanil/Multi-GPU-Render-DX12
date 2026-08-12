#pragma once
#include "Component.h"
#include "d3dApp.h"

using namespace DirectX;
using namespace SimpleMath;

class Rotater :
    public Component
{
public:
    Rotater(const float speed = 1, const Vector3 rotationAxis = Vector3::UnitX)
        : rotationAxis(rotationAxis), speed(speed)
    {
    }

    void Reset() { currentTime = 0.0f; invers = false; }

private:
    void Update() override;

    const float time = 2;
    float currentTime = 0;
    bool invers = false;


    Vector3 rotationAxis;
    float speed;
};
