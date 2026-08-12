#include "pch.h"
#include "Rotater.h"
#include "GameObject.h"
#include "Transform.h"


void Rotater::Update()
{
    const float dt = Common::D3DApp::GetApp().GetTimer()->DeltaTime();

    auto tr = gameObject->GetTransform();

    tr->AdjustEulerRotation(rotationAxis * (speed * dt));
}
