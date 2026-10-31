#pragma once
#include "Component.h"
#include "BezierCurve.h"
#include "GameObject.h"
#include "Transform.h"

class SplineController : public Component {
public:
    /*
    Example:
    std::vector<Vector3> controlPoints = {
    Vector3(-100, 100, -100),
    Vector3(-100, 100, 100),
    Vector3(100, 100, 100),
    Vector3(100, 100, -100)
    };
    BezierCurve curve(controlPoints);*/
    SplineController(const BezierCurve& curve, float speed = 1.0f);

    void Update(const PEPEngine::Utils::GameTimer* gt) override;

    void Play();
    void Stop();
    void Reset();

    void SetOffset(float t);
    float GetOffset() const;

    void SetSpeed(float speed);
    float GetSpeed() const;

    void SetLooping(bool looping);
    bool GetLooping() const;

    void SetPingPong(bool pingPong);
    bool GetPingPong() const;
    
    void SetCurve(const BezierCurve& curve);
    const BezierCurve& GetCurve() const;

private:
    BezierCurve m_curve;
    float m_speed;
    float m_currentOffset;
    bool m_isPlaying; // default true in constructor

    bool m_isLooping = true;
    bool m_isPingPong = false;
    int m_direction = 1;
};

