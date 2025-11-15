#include "pch.h"
#include "SplineController.h"
#include "d3dApp.h"

SplineController::SplineController(const BezierCurve& curve, float speed)
    : m_curve(curve)
    , m_speed(speed)
    , m_currentOffset(0.0f)
    , m_isPlaying(true)
{
}

void SplineController::Update(const PEPEngine::Utils::GameTimer* gt)
{
    if (!m_isPlaying) return;
    if (m_currentOffset > 1.0f) {
        if (m_isPingPong) {
            m_currentOffset = 2.0f - m_currentOffset;
            m_direction = -1;
        }
        else if (m_isLooping) {
            m_currentOffset -= 1.0f;
        }
    }
    else if (m_currentOffset < 0.0f) {
        if (m_isPingPong) {
            m_currentOffset = -m_currentOffset;
            m_direction = 1;
        }
        else if (m_isLooping) {
            m_currentOffset += 1.0f;
        }
    }

    const float dt = Common::D3DApp::GetApp().GetTimer()->DeltaTime();
    m_currentOffset += m_direction * m_speed *dt;

    Vector3 newPosition = m_curve.Evaluate(m_currentOffset);

    this->gameObject->GetTransform()->SetPosition(newPosition);
}

void SplineController::Play()
{
    m_isPlaying = true;
}

void SplineController::Stop()
{
    m_isPlaying = false;
}

void SplineController::Reset()
{
    m_currentOffset = 0.0f;
    m_direction = 1;
}

void SplineController::SetOffset(float t)
{
    m_currentOffset = t;
}

float SplineController::GetOffset() const
{
    return m_currentOffset;
}

// getters and setters
void SplineController::SetSpeed(float speed)
{
    m_speed = speed;
}

float SplineController::GetSpeed() const
{
    return m_speed;
}

void SplineController::SetLooping(bool looping)
{
    m_isLooping = looping;
}

bool SplineController::GetLooping() const
{
    return m_isLooping;
}

void SplineController::SetPingPong(bool pingPong)
{
    m_isPingPong = pingPong;
}

bool SplineController::GetPingPong() const
{
    return m_isPingPong;
}

void SplineController::SetCurve(const BezierCurve& curve)
{
    m_curve = curve;
}

const BezierCurve& SplineController::GetCurve() const
{
    return m_curve;
}