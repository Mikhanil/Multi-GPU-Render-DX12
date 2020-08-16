#pragma once
#include "Component.h"
#include "Keyboard.h"

class ObjectMover :
	public Component
{
	Keyboard* keyboard;

	void Draw(std::shared_ptr<GCommandList> cmdList) override;;

	void Update() override;;
public:
	ObjectMover();;
};
