#pragma once
#include "Component.h"
#include "GameObject.h"

struct LightData
{
    DirectX::XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };
    float FalloffStart = 1.0f;                          // point/spot light only
    DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };// directional/spot light only
    float FalloffEnd = 10.0f;                           // point/spot light only
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  // point/spot light only
    float SpotPower = 64.0f;                            // spot light only
};

enum LightType
{
	Directional,
	Point,
	Spot
};

class Light :	public Component
{
private:
	const DirectX::XMFLOAT3 spotDirectionalDirection = { 0.0f, -1.0f, 0.0f };
    int NumFramesDirty = globalCountFrameResources;
    LightData lightData{};

	void Update() override
	{
        if (NumFramesDirty > 0)
        {
            lightData.Strength = strength;
            lightData.FalloffEnd = falloffEnd;
            lightData.FalloffStart = falloffStart;
            lightData.Position = gameObject->GetTransform()->GetPosition();
            lightData.SpotPower = spotPower;
            lightData.Direction = direction;

            NumFramesDirty--;
        }
       
	};
    void Draw(ID3D12GraphicsCommandList* cmdList) override {};
	
    DirectX::XMFLOAT3 strength = { 0.5f, 0.5f, 0.5f };
    DirectX::XMFLOAT3 direction = { 0, 0, 0 };
    float falloffStart = 1.0f;
    float falloffEnd = 10.0f;
    float spotPower = 64.0f;
    LightType type;
	
public:

    LightData GetData() const { return  lightData; }
	
    LightType Type() const
    {
        return type;
    }
	
    Light(LightType type = LightType::Directional) : type(type) {};

    DirectX::XMFLOAT3 Direction() const { return direction; }

	void Direction(DirectX::XMFLOAT3 direct)
    {
        direction = direct;
        NumFramesDirty = globalCountFrameResources;
    }
	
    DirectX::XMFLOAT3 Strength() const
    {
	    return strength;
    }
	
	void Strength(DirectX::XMFLOAT3 strength)
    {
        this->strength = strength;
        NumFramesDirty = globalCountFrameResources;
    }
	
    void FalloffStart(float start)
    {
        falloffStart = start;
        NumFramesDirty = globalCountFrameResources;
    }
	
	float FalloffStart() const
	{
        return falloffStart;
    }

	void FalloffEnd(float end)
    {
        falloffEnd = end;
        NumFramesDirty = globalCountFrameResources;
    }
	
	float FalloffEnd() const
	{
        return falloffEnd;
    }

	void SpotPower(float power)
    {
        spotPower = power;
        NumFramesDirty = globalCountFrameResources;
    }

	float SpotPower() const
	{
        return spotPower;
    }

};

