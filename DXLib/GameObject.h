#pragma once
#include "d3dUtil.h"
#include "Transform.h"
#include "Renderer.h"

class GameObject
{
public:

	GameObject(ID3D12Device* device);

	GameObject(ID3D12Device* device, std::string name);

	GameObject(ID3D12Device* device, std::string name, Vector3 position, Vector3 scale, Quaternion rotate);

	void virtual Update();

	void virtual Draw(ID3D12GraphicsCommandList* cmdList);

	Transform* GetTransform() const;

	Renderer* GetRenderer();

	template <class T = Component>
	void AddComponent(T* component);

	template <class T = Component>
	T* GetComponent();

protected:


	custom_vector<Component*> components = DXAllocator::CreateVector<Component*>();
	std::unique_ptr<Transform> transform = nullptr;
	Renderer* renderer = nullptr;
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
