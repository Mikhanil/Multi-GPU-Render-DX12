#pragma once
#include "Component.h"

class GCommandList;
class GameTimer;
class Mouse;
class Keyboard;

using namespace DirectX::SimpleMath;
class CameraController :
	public Component
{
	Keyboard* keyboard;
	Mouse* mouse;
	GameTimer* timer;


	double xMouseSpeed = 100;
	double yMouseSpeed = 70;

public:

	CameraController();

	void Update() override;;
	void Draw(std::shared_ptr<GCommandList> cmdList) override;;
};
