#include "CameraController.h"
#include "ShapesApp.h"

CameraController::CameraController()
{
	auto app = static_cast<ShapesApp*>(ShapesApp::GetApp());
	keyboard = app->GetKeyboard();
	mouse = app->GetMouse();
	timer = app->GetTimer();
}

void CameraController::Update()
{
	while (!keyboard->CharBufferIsEmpty())
	{
		unsigned char ch = keyboard->ReadChar();
	}

	while (!keyboard->KeyBufferIsEmpty())
	{
		KeyboardEvent kbe = keyboard->ReadKey();
		unsigned char keycode = kbe.GetKeyCode();
	}

	float cameraSpeed = 6.0f;
	float dt = timer->DeltaTime();

	auto tr = gameObject->GetTransform();

	if (keyboard->KeyIsPressed(VK_SHIFT))
	{
		cameraSpeed *= 10;
	}

	while (!mouse->EventBufferIsEmpty())
	{
		MouseEvent me = mouse->ReadEvent();
		if (mouse->IsRightDown())
		{
			if (me.GetType() == MouseEvent::EventType::RAW_MOVE)
			{
				gameObject->GetTransform()->AdjustEulerRotation(-1 * (float)me.GetPosY() * dt * yMouseSpeed, (float)me.GetPosX() * dt * xMouseSpeed,0);
			}
		}
	}

	if (keyboard->KeyIsPressed('W'))
	{
		tr->AdjustPosition(tr->GetForwardVector() * cameraSpeed * dt);
	}
	if (keyboard->KeyIsPressed('S'))
	{
		tr->AdjustPosition(tr->GetBackwardVector() * cameraSpeed * dt);
	}
	if (keyboard->KeyIsPressed('A'))
	{
		tr->AdjustPosition(tr->GetLeftVector() * cameraSpeed * dt);
	}
	if (keyboard->KeyIsPressed('D'))
	{
		tr->AdjustPosition(tr->GetRightVector() * cameraSpeed * dt);
	}
	if (keyboard->KeyIsPressed(VK_SPACE))
	{
		tr->AdjustPosition(tr->GetUpVector() * cameraSpeed * dt);
	}
	if (keyboard->KeyIsPressed('Z'))
	{
		tr->AdjustPosition( tr->GetDownVector() * cameraSpeed * dt);
	}
}

void CameraController::Draw(ID3D12GraphicsCommandList* cmdList)
{
}
