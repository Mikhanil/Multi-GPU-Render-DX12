#pragma once
#include "Component.h"
#include "d3dApp.h"

#include <memory>

using namespace DirectX;
using namespace SimpleMath;

class Transform;

class Orbiter : public Component
{
public:
    Orbiter(const std::shared_ptr<Transform>& center,
        const Vector3& offset,
        float orbitSpeedRad = 1.0f,
        const Vector3& selfEulerSpeedDeg = Vector3::Zero);

private:
    void Update() override;

    std::weak_ptr<Transform> center;
    Vector3 offset;
    float orbitSpeedRad = 1.0f;
    float angleRad = 0.0f;
    Vector3 selfEulerSpeedDeg = Vector3::Zero;
};
    