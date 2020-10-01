#include "pch.h"
#include "GameObject.h"
#include <utility>
#include "Component.h"
#include "ModelRenderer.h"
#include "Transform.h"

GameObject::GameObject() : GameObject( "Game Object")
{
};

GameObject::GameObject(std::string name)
	: GameObject( std::move(name), Vector3::Zero,
	             Vector3::One, Quaternion::Identity)
{
}

GameObject::
GameObject(std::string name, Vector3 position, Vector3 scale, Quaternion rotate): name(
	std::move(name))
{
	transform = std::make_shared<Transform>( position, rotate, scale);

	AddComponent(transform);
}

void GameObject::Update()
{
	for (auto& component : components)
	{
		component->Update();
	}
}

void GameObject::Draw(std::shared_ptr<GCommandList> cmdList)
{
	for (auto&& component : components)
	{
		component->Draw(cmdList);
	}
}

std::shared_ptr<Transform> GameObject::GetTransform() const
{
	return transform;
}

std::shared_ptr<ModelRenderer> GameObject::GetRenderer() 
{
	if (renderer == nullptr)
	{
		for (auto&& component : components)
		{
			const auto comp = dynamic_cast<ModelRenderer*>(component.get());
			if (comp)
			{
				renderer = std::static_pointer_cast<ModelRenderer>(component);
				break;
			}
		}
	}

	return renderer;
}

void GameObject::SetScale(float scale) const
{
	transform->SetScale(Vector3(scale, scale, scale));
}

void GameObject::SetScale(Vector3& scale) const
{
	transform->SetScale(scale);
}
