#include "pch.h"
#include "BezierCurve.h"
#include <stdexcept>

using namespace DirectX::SimpleMath;

BezierCurve::BezierCurve(const std::vector<DirectX::SimpleMath::Vector3>& controlPoints)
    : m_controlPoints(controlPoints)
{
    if (controlPoints.size() < 4) {
        throw std::runtime_error("BezierCurve needs at least 4 points");
    }
}

Vector3 BezierCurve::Evaluate(float t) const {
    const Vector3& P0 = m_controlPoints[0];
    const Vector3& P1 = m_controlPoints[1];
    const Vector3& P2 = m_controlPoints[2];
    const Vector3& P3 = m_controlPoints[3];

    // B(t) = (1-t)³P0 + 3(1-t)²tP1 + 3(1-t)t²P2 + t³P3
    float t2 = t * t;
    float t3 = t2 * t;
    float mt = 1.0f - t;
    float mt2 = mt * mt;
    float mt3 = mt2 * mt;

    return P0 * mt3 +
        P1 * (3.0f * mt2 * t) +
        P2 * (3.0f * mt * t2) +
        P3 * t3;
}

const std::vector<DirectX::SimpleMath::Vector3>& BezierCurve::GetControlPoints() const
{
    return m_controlPoints;
}


