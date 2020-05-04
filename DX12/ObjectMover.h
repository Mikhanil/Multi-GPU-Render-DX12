#pragma once
#include "Component.h"
#include "Keyboard.h"

class ObjectMover :
	public Component
{

	Keyboard* keyboard;

	void Draw(ID3D12GraphicsCommandList* cmdList) override;;

	void Update() override;;
public:
	ObjectMover();;
};

