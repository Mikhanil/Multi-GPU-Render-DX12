#pragma once
#include "Component.h"
#include "d3dUtil.h"
#include "ShaderBuffersData.h"


using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;


class Light : public Component
{
    const DirectX::XMFLOAT3 spotDirectionalDirection = {0.0f, -1.0f, 0.0f};
    int NumFramesDirty = globalCountFrameResources;
    LightData lightData{};

    void Update() override;

    DirectX::XMFLOAT3 strength = {0.5f, 0.5f, 0.5f};
    DirectX::XMFLOAT3 direction = {0, 0, 0};
    float falloffStart = 1.0f;
    float falloffEnd = 10.0f;
    float spotPower = 64.0f;
    LightType type;

    Matrix view = Matrix::Identity;
    Matrix projection = Matrix::Identity;

public:
    LightData GetData() const;

    LightType Type() const;

    Light(LightType type = Directional);;

    DirectX::XMFLOAT3 Direction() const;

    void Direction(DirectX::XMFLOAT3 direct);

    DirectX::XMFLOAT3 Strength() const;

    void Strength(DirectX::XMFLOAT3 strength);

    void FalloffStart(float start);

    float FalloffStart() const;

    void FalloffEnd(float end);

    float FalloffEnd() const;

    void SpotPower(float power);

    float SpotPower() const;
};
