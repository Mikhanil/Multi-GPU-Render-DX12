#include "ReflectionBenchmarkState.h"

#include "../ReflectionApp.h"

namespace Common
{
    ReflectionBenchmarkState::ReflectionBenchmarkState(ReflectionApp& app,
                                                         const bool useSecondGpuForSsr,
                                                         const bool useSecondGpuForReflectionProbes,
                                                         const bool dynamicReflectionProbes,
                                                         std::wstring name,
                                                         const uint32_t durationInSeconds,
                                                         const FileQueueWriter& writer)
        : BenchmarkState(writer),
          app(app),
          useSecondGpuForSsr(useSecondGpuForSsr),
          useSecondGpuForReflectionProbes(useSecondGpuForReflectionProbes),
          dynamicReflectionProbes(dynamicReflectionProbes),
          name(std::move(name)),
          durationInSeconds(durationInSeconds)
    {
    }

    void ReflectionBenchmarkState::Enter()
    {
        BenchmarkState::Enter();
        samplesTaken = 0;
        frameNumber = 0;
        app.SetReflectionBenchmarkConfiguration(useSecondGpuForSsr, useSecondGpuForReflectionProbes,
                                                dynamicReflectionProbes);
        fileQueueWriter.PushMessage(L"Type;Frame;FrameTimeMs;FPS;MinFPS;MinMSPF;MaxFPS;MaxMSPF");
    }

    void ReflectionBenchmarkState::Tick(const float deltaTime)
    {
        ++frameNumber;
        const float frameTimeMs = deltaTime * 1000.0f;
        const float fps = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;
        fileQueueWriter.PushMessage(
            L"Frame;" + std::to_wstring(frameNumber) + L";" + std::to_wstring(frameTimeMs) + L";" +
            std::to_wstring(fps) + L";;;;");

        BenchmarkState::Tick(deltaTime);
    }

    void ReflectionBenchmarkState::Exit()
    {
        fileQueueWriter.WriteAllLog();
        BenchmarkState::Exit();
    }

    bool ReflectionBenchmarkState::IsCompleted()
    {
        return samplesTaken >= durationInSeconds;
    }

    void ReflectionBenchmarkState::OnStatsCalculated(const TimeStats& stats)
    {
        ++samplesTaken;
        fileQueueWriter.PushMessage(
            L"Second;" + std::to_wstring(samplesTaken) + L";" + std::to_wstring(stats.mspf) + L";" +
            std::to_wstring(stats.fps) + L";" +
            std::to_wstring(stats.minFps) + L";" + std::to_wstring(stats.minMspf) + L";" +
            std::to_wstring(stats.maxFps) + L";" + std::to_wstring(stats.maxMspf));

        app.SetReflectionBenchmarkTitle(name, samplesTaken * 100 / durationInSeconds, stats.fps);
    }
}
