#include "pch.h"
#include "Rotater.h"
#include "GameObject.h"
#include "Transform.h"


void Rotater::Update()
{
	const float dt = DXLib::D3DApp::GetApp().GetTimer()->DeltaTime();
	
	auto tr = gameObject->GetTransform();
	
	tr->AdjustEulerRotation(Vector3(speed * dt, 0, 0));
}
