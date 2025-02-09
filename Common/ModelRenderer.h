#pragma once
#include "Renderer.h"


using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;

class Transform;
class GModel;

class ModelRenderer : public Renderer
{
protected:
    ObjectConstants objectWorldData{};
    std::shared_ptr<ConstantUploadBuffer<ObjectConstants>> modelDataBuffer = nullptr;
    std::shared_ptr<GDevice> device;
    std::shared_ptr<GModel> model;


    void Draw(std::shared_ptr<GCommandList> cmdList) override;

    void Update() override;

public:
    ModelRenderer(const std::shared_ptr<GDevice>& device, const std::shared_ptr<GModel>& model);

    void SetModel(const std::shared_ptr<GModel>& asset);
};
