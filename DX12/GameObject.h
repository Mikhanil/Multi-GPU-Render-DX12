#pragma once
#include "d3dUtil.h"
#include "Transform.h"
#include "Renderer.h"


class GameObject
{


	
public:

	GameObject(ID3D12Device* device, std::string name, Vector3 position, Vector3 scale, Quaternion rotate): name(std::move(name))
	{
		transform = std::make_unique<Transform>(device, position, rotate, scale);
		transform->gameObject = this;

		components.push_back((transform.get()));
	}
	
	void Update();

	void Draw(ID3D12GraphicsCommandList* cmdList);

	template<class T = Component>
	void AddComponent(T* component)
	{
		component->gameObject = this;
		components.push_back(component);
	}

	Transform* GetTransform() const
	{
		return transform.get();
	}

	Renderer* GetRenderer()
	{
		if(renderer == nullptr)
		{
			for (auto && component : components)
			{
				const auto comp = dynamic_cast<Renderer*> (component);
				if(comp)
				{
					renderer = (comp);
					break;
				}
			}
		}

		return renderer;
	}
	
protected:

	std::vector<Component*> components;
	std::unique_ptr<Transform> transform = nullptr;
	Renderer* renderer = nullptr;
	std::string name;
};

