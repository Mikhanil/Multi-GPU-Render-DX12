#include "pch.h"
#include "Orbiter.h"

#include "GameObject.h"
#include "Transform.h"

Orbiter::Orbiter(const std::shared_ptr<Transform>& center,
    const Vector3& offset,
    float orbitSpeedRad,
    const Vector3& selfEulerSpeedDeg)
    : center(center),
    offset(offset),
    orbitSpeedRad(orbitSpeedRad),
    selfEulerSpeedDeg(selfEulerSpeedDeg)
{
}

void Orbiter::Update()
{
    const float dt = Common::D3DApp::GetApp().GetTimer()->DeltaTime();

    auto centerLocked = center.lock();
    if (!centerLocked) return;

    angleRad += orbitSpeedRad * dt;

    Matrix R = Matrix::CreateRotationY(angleRad);
    Vector3 rotatedOffset = Vector3::TransformNormal(offset, R);

    auto tr = gameObject->GetTransform();
    tr->SetPosition(centerLocked->GetWorldPosition() + rotatedOffset);

    tr->AdjustEulerRotation(selfEulerSpeedDeg * dt);
}
