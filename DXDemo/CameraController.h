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


	double xMouseSpeed = 180;
	double yMouseSpeed = 120;

public:

	CameraController();

	void Update() override;;
	void Draw(std::shared_ptr<GCommandList> cmdList) override;;
};
