#include "GameObject.h"
#include <utility>
#include "Component.h"
#include "Transform.h"

GameObject::GameObject(ID3D12Device* device) : GameObject(device, "Game Object"){};

GameObject::GameObject(ID3D12Device* device, std::string name)
: GameObject(device, std::move(name), Vector3::Zero, 
	Vector3::One, Quaternion::Identity)
{
	objectWorldPositionBuffer = std::make_unique<ConstantBuffer<ObjectConstants>>(device, 1);
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
	if(transform->IsDirty())
	{
		bufferConstant.TextureTransform = transform->TextureTransform.Transpose();
		bufferConstant.World = transform->GetWorldMatrix().Transpose();
		if (GetRenderer() != nullptr && renderer->Material != nullptr)
		{
			bufferConstant.materialIndex = renderer->Material->GetIndex();			
		}
		objectWorldPositionBuffer->CopyData(0, bufferConstant);
	}

	
	
	for (auto& component : components)
	{
		component->Update();
	}
}

void GameObject::Draw(ID3D12GraphicsCommandList* cmdList)
{
	cmdList->SetGraphicsRootConstantBufferView(StandardShaderSlot::ObjectData, objectWorldPositionBuffer->Resource()->GetGPUVirtualAddress());

	
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