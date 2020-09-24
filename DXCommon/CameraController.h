#pragma once
#include "Component.h"

class GCommandList;
class GameTimer;
class Mousepad;
class KeyboardDevice;

using namespace DirectX::SimpleMath;
class CameraController :
	public Component
{
	KeyboardDevice* keyboard;
	Mousepad* mouse;
	GameTimer* timer{};


	double xMouseSpeed = 100;
	double yMouseSpeed = 70;

public:

	CameraController();

	void Update() override;;
	void Draw(std::shared_ptr<GCommandList> cmdList) override;;
};
