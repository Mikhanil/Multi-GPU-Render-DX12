#include "pch.h"
#include "Light.h"
#include "GameObject.h"
#include "Transform.h"


void Light::Update()
{
    if (NumFramesDirty > 0)
    {
        lightData.Strength = strength;
        lightData.FalloffEnd = falloffEnd;
        lightData.FalloffStart = falloffStart;
        lightData.Position = gameObject->GetTransform()->GetWorldPosition();
        lightData.SpotPower = spotPower;
        lightData.Direction = direction;

        NumFramesDirty--;
    }
}

LightData Light::GetData() const
{
    return lightData;
}

LightType Light::Type() const
{
    return type;
}

Light::Light(LightType type): type(type)
{
}

DirectX::XMFLOAT3 Light::Direction() const
{
    return direction;
}

void Light::Direction(const DirectX::XMFLOAT3 direct)
{
    direction = direct;
    NumFramesDirty = globalCountFrameResources;
}

DirectX::XMFLOAT3 Light::Strength() const
{
    return strength;
}

void Light::Strength(const DirectX::XMFLOAT3 strength)
{
    this->strength = strength;
    NumFramesDirty = globalCountFrameResources;
}

void Light::FalloffStart(const float start)
{
    falloffStart = start;
    NumFramesDirty = globalCountFrameResources;
}

float Light::FalloffStart() const
{
    return falloffStart;
}

void Light::FalloffEnd(const float end)
{
    falloffEnd = end;
    NumFramesDirty = globalCountFrameResources;
}

float Light::FalloffEnd() const
{
    return falloffEnd;
}

void Light::SpotPower(const float power)
{
    spotPower = power;
    NumFramesDirty = globalCountFrameResources;
}

float Light::SpotPower() const
{
    return spotPower;
}
