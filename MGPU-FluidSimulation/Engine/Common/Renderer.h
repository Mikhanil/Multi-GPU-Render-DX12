#pragma once
#include "Component.h"
#include "Material.h"


class Renderer : public Component
{
public:
    Renderer(): Component()
    {
    } ;

    void Update(const PEPEngine::Utils::GameTimer* gt) override = 0;
    virtual void Draw(const std::shared_ptr<GCommandList>& cmdList) = 0;
};
