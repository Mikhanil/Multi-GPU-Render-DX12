#include "GameObject.h"

namespace GameEngine
{
	GameEngine::GameObject::GameObject()
	{
		AddComponent(new Transform());
	}

	void GameEngine::GameObject::AddComponent(Component* component)
	{
		components.push_back(component);
	}

	Transform* GameObject::GetTransform()
	{
		if (transform == nullptr)
		{
			for (auto component : components)
			{
				auto ptr = component;
				if (dynamic_cast<Transform*>(ptr) != nullptr)
				{
					transform = ((Transform*)ptr);
					break;
				}
			}
		}

		return transform;
	}

	
}

