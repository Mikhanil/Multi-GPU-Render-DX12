#pragma once
#include "pch.h"
#include "SimpleMath.h"
#include <vector>


class BezierCurve {
public:
    BezierCurve(const std::vector<DirectX::SimpleMath::Vector3>& controlPoints);

    DirectX::SimpleMath::Vector3 Evaluate(float t) const;

    const std::vector<DirectX::SimpleMath::Vector3>& GetControlPoints() const;

private:
    std::vector<DirectX::SimpleMath::Vector3> m_controlPoints;
};

