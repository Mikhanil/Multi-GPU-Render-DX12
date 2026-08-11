#pragma once

#include "Services/States/BenchmarkState.h"

namespace Common
{
    class ReflectionApp;

    // A benchmark step owns the application's reflection flags and restores
    // them before sampling, so timings from adjacent modes cannot overlap.
    class ReflectionBenchmarkState final : public BenchmarkState
    {
    public:
        ReflectionBenchmarkState(ReflectionApp& app, bool useSecondGpuForSsr,
                                 bool useSecondGpuForReflectionProbes, bool dynamicReflectionProbes,
                                 std::wstring name,
                                 uint32_t durationInSeconds, const FileQueueWriter& writer);

        void Enter() override;
        void Tick(float deltaTime) override;
        void Exit() override;
        bool IsCompleted() override;

    protected:
        void OnStatsCalculated(const TimeStats& stats) override;

    private:
        ReflectionApp& app;
        bool useSecondGpuForSsr;
        bool useSecondGpuForReflectionProbes;
        bool dynamicReflectionProbes;
        std::wstring name;
        uint32_t durationInSeconds;
        uint32_t samplesTaken = 0;
        uint64_t frameNumber = 0;
    };
}
