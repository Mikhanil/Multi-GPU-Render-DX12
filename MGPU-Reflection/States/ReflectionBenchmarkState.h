#pragma once

#include "Services/States/BenchmarkState.h"
#include <cstdint>

class HybridCubeMapApp;

// A state deliberately separates reset/prewarm from sampled seconds so the
// one-time baked cubemap capture is never included in a result row.
class ReflectionBenchmarkState final : public BenchmarkState
{
public:
    ReflectionBenchmarkState(HybridCubeMapApp& app, bool useOnlyPrime, std::wstring name,
                             uint32_t durationInSeconds, const FileQueueWriter& writer);

    void Enter() override;
    void Exit() override;
    void Tick(float deltaTime) override;
    bool IsCompleted() override;

protected:
    void OnStatsCalculated(const TimeStats& stats) override;

private:
    HybridCubeMapApp& app;
    bool useOnlyPrime;
    std::wstring name;
    uint32_t durationInSeconds;
    float settleElapsed = 0.0f;
    uint32_t samplesTaken = 0;
    float totalFps = 0.0f;
    bool sampling = false;
    static constexpr float SettleSeconds = 2.0f;
};
