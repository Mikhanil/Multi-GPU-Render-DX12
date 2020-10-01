#pragma once

#include "d3d12.h"
#include "SimpleMath.h"
#include "MemoryAllocator.h"

using namespace DirectX::SimpleMath;

class GCommandList;
class ModelRenderer;
class Component;
class Transform;
class Renderer;

class GameObject
{
public:

	GameObject();

	GameObject(std::string name);

	GameObject(std::string name, Vector3 position, Vector3 scale, Quaternion rotate);

	void virtual Update();

	void virtual Draw(std::shared_ptr<GCommandList> cmdList);

	std::shared_ptr<Transform> GetTransform() const;

	std::shared_ptr<ModelRenderer> GetRenderer();

	template <class T = Component>
	void AddComponent(std::shared_ptr<T> component);

	template <class T = Component>
	std::shared_ptr<T> GetComponent();

	void SetScale(float scale) const;

	void SetScale(Vector3& scale) const;

	std::string& GetName() { return  name; }
	
protected:


	custom_vector<std::shared_ptr<Component>> components = MemoryAllocator::CreateVector<std::shared_ptr<Component>>();
	std::shared_ptr<Transform> transform = nullptr;
	std::shared_ptr<ModelRenderer> renderer = nullptr;
	std::string name;
};

template <class T = Component>
void GameObject::AddComponent(std::shared_ptr<T> component)
{
	component->gameObject = this;
	components.push_back(component);
}

template <class T = Component>
std::shared_ptr<T> GameObject::GetComponent()
{
	for (auto&& component : components)
	{
		auto ptr = component.get();
		if (dynamic_cast<T*>(ptr) != nullptr)
		{
			return std::static_pointer_cast<T>(component);
		}
	}
	return nullptr;
}
