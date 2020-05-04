#include "ObjectMover.h"
#include "ShapesApp.h"

void ObjectMover::Draw(ID3D12GraphicsCommandList* cmdList)
{
}

void ObjectMover::Update()
{
	const float dt = ((ShapesApp*)ShapesApp::GetApp())->GetTimer()->DeltaTime();

	Vector3 offset = Vector3::Zero;
	if (keyboard->KeyIsPressed(VK_UP))
	{
		offset.y += 1.0f * dt;
	}
	if (keyboard->KeyIsPressed(VK_DOWN))
	{
		offset.y -= 1.0f * dt;
	}
	if (keyboard->KeyIsPressed(VK_LEFT))
	{
		offset.x -= 1.0f * dt;
	}
	if (keyboard->KeyIsPressed(VK_RIGHT))
	{
		offset.x += 1.0f * dt;
	}

	if (offset != Vector3::Zero)
		gameObject->GetTransform()->AdjustPosition(offset);
}

ObjectMover::ObjectMover()
{
	keyboard = ((ShapesApp*)ShapesApp::GetApp())->GetKeyboard();
}
