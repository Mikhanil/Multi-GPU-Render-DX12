#pragma once
#include "Component.h"
#include <vector>
#include "Transform.h"

namespace GameEngine
{
	
	class GameObject
	{
	public:
		GameObject();

		void AddComponent(Component* component);

		Transform* GetTransform();

		void virtual Update() = 0;

	protected:
		Transform* transform = nullptr;
		std::vector<Component*> components;
	};
}

