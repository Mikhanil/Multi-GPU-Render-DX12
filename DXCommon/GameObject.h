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

	Transform* GetTransform() const;

	ModelRenderer* GetRenderer();

	template <class T = Component>
	void AddComponent(T* component);

	template <class T = Component>
	T* GetComponent();

	void SetScale(float scale) const;

	void SetScale(Vector3& scale) const;

	std::string& GetName() { return  name; }
	
protected:


	custom_vector<Component*> components = MemoryAllocator::CreateVector<Component*>();
	std::unique_ptr<Transform> transform = nullptr;
	ModelRenderer* renderer = nullptr;
	std::string name;
};

template <class T>
void GameObject::AddComponent(T* component)
{
	component->gameObject = this;
	components.push_back(component);
}

template <class T = Component>
T* GameObject::GetComponent()
{
	for (auto&& component : components)
	{
		auto ptr = component;
		if (dynamic_cast<T*>(ptr) != nullptr)
		{
			return static_cast<T*>(ptr);
		}
	}
	return nullptr;
}
