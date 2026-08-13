#pragma once

#include "Services/States/BenchmarkState.h"
#include <cstdint>

namespace Common
{
    class ReflectionApp;

    // A benchmark step applies its adapter placement and SSAA configuration
    // before sampling. Resources stay cached across adjacent states of one GPU
    // pair (including its adaptive SSAA levels) and are recreated when the
    // benchmark advances to another pair.
    class ReflectionBenchmarkState final : public BenchmarkState
    {
    public:
        ReflectionBenchmarkState(ReflectionApp& app, uint32_t gpuPairContextIndex, bool useSecondGpuForSsr,
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
        uint32_t gpuPairContextIndex;
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
