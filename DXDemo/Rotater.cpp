#include "Rotater.h"
#include "GameObject.h"


void Rotater::Update()
{
	const float dt = DXLib::D3DApp::GetApp().GetTimer()->DeltaTime();
	
	auto tr = gameObject->GetTransform();
	
	tr->AdjustEulerRotation(Vector3(speed * dt, 0, 0));
}
