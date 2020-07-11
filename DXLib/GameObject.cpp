#include "GameObject.h"
#include <utility>
#include "Component.h"
#include "Transform.h"

GameObject::GameObject(ID3D12Device* device) : GameObject(device, "Game Object")
{
};

GameObject::GameObject(ID3D12Device* device, std::string name)
	: GameObject(device, std::move(name), Vector3::Zero,
	             Vector3::One, Quaternion::Identity)
{
}

GameObject::
GameObject(ID3D12Device* device, std::string name, Vector3 position, Vector3 scale, Quaternion rotate): name(
	std::move(name))
{
	transform = std::make_unique<Transform>(device, position, rotate, scale);

	AddComponent(transform.get());
}

void GameObject::Update()
{
	for (auto& component : components)
	{
		component->Update();
	}
}

void GameObject::Draw(ID3D12GraphicsCommandList* cmdList)
{
	for (auto&& component : components)
	{
		component->Draw(cmdList);
	}
}

Transform* GameObject::GetTransform() const
{
	return transform.get();
}

Renderer* GameObject::GetRenderer()
{
	if (renderer == nullptr)
	{
		for (auto&& component : components)
		{
			const auto comp = dynamic_cast<Renderer*>(component);
			if (comp)
			{
				renderer = (comp);
				break;
			}
		}
	}

	return renderer;
}
