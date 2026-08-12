#pragma once

#include "Services/States/BenchmarkState.h"
#include <cstdint>

namespace Common
{
    class ReflectionApp;

    // A benchmark step applies its adapter placement and SSAA configuration
    // before sampling; baked probe resources remain cached across states.
    class ReflectionBenchmarkState final : public BenchmarkState
    {
    public:
        ReflectionBenchmarkState(ReflectionApp& app, bool useSecondGpuForSsr,
                                 uint32_t primaryProbeCount, bool dynamicReflectionProbes,
                                 bool updateOneProbeFacePerFrame, uint32_t ssaaMultiplier,
                                 bool isSsaaExpansionProbe, bool endsSsaaLevel, std::wstring name,
                                 uint32_t durationInSeconds, const FileQueueWriter& writer);

        void Enter() override;
        void Exit() override;
        bool IsCompleted() override;

    protected:
        void OnStatsCalculated(const TimeStats& stats) override;

    private:
        ReflectionApp& app;
        bool useSecondGpuForSsr;
        uint32_t primaryProbeCount;
        bool dynamicReflectionProbes;
        bool updateOneProbeFacePerFrame;
        uint32_t ssaaMultiplier;
        bool isSsaaExpansionProbe;
        bool endsSsaaLevel;
        std::wstring name;
        uint32_t durationInSeconds;
        uint32_t samplesTaken = 0;
        float totalFps = 0.0f;
    };
}
