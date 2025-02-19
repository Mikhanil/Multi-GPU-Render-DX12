#pragma once

#include "d3d12.h"
#include "GCommandList.h"
#include "SimpleMath.h"
#include "MemoryAllocator.h"
#include "ModelRenderer.h"

using namespace DirectX::SimpleMath;

using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;

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

    void virtual Draw(const std::shared_ptr<GCommandList>& cmdList);

    std::shared_ptr<Transform>& GetTransform();

    std::shared_ptr<Renderer>& GetRenderer();

    template <class T = Component>
    void AddComponent(std::shared_ptr<T> component)
    {
        component->gameObject = this;
        components.push_back(component);
    }

    template <class T = Component>
    inline std::shared_ptr<T> GetComponent()
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

    void SetScale(float scale) const;

    void SetScale(Vector3& scale) const;

    std::string& GetName() { return name; }

protected:
    custom_vector<std::shared_ptr<Component>> components = MemoryAllocator::CreateVector<std::shared_ptr<Component>>();
    std::shared_ptr<Transform> transform = nullptr;
    std::shared_ptr<Renderer> renderer = nullptr;
    std::string name;
};
