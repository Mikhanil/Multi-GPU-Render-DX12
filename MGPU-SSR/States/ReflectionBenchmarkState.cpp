#include "ReflectionBenchmarkState.h"

#include "../ReflectionApp.h"
#include "Utils.h"

namespace Common
{
    ReflectionBenchmarkState::ReflectionBenchmarkState(ReflectionApp& app,
                                                         const uint32_t gpuPairContextIndex,
                                                         const bool useSecondGpuForSsr,
                                                         const uint32_t primaryProbeCount,
                                                         const bool dynamicReflectionProbes,
                                                         const bool updateOneProbeFacePerFrame,
                                                         const uint32_t ssaaMultiplier,
                                                         const bool isSsaaExpansionProbe,
                                                         const bool endsSsaaLevel,
                                                         std::wstring name,
                                                         const uint32_t durationInSeconds,
                                                         const FileQueueWriter& writer)
        : BenchmarkState(writer),
          app(app),
          gpuPairContextIndex(gpuPairContextIndex),
          useSecondGpuForSsr(useSecondGpuForSsr),
          primaryProbeCount(primaryProbeCount),
          dynamicReflectionProbes(dynamicReflectionProbes),
          updateOneProbeFacePerFrame(updateOneProbeFacePerFrame),
          ssaaMultiplier(ssaaMultiplier),
          isSsaaExpansionProbe(isSsaaExpansionProbe),
          endsSsaaLevel(endsSsaaLevel),
          name(std::move(name)),
          durationInSeconds(durationInSeconds)
    {
    }

    void ReflectionBenchmarkState::Enter()
    {
        BenchmarkState::Enter();
        samplesTaken = 0;
        totalFps = 0.0f;
        app.SetReflectionBenchmarkConfiguration(gpuPairContextIndex, useSecondGpuForSsr, primaryProbeCount,
                                                dynamicReflectionProbes, updateOneProbeFacePerFrame,
                                                ssaaMultiplier);
        fileQueueWriter.PushMessage(L"FPS;MSPF;MinFPS;MinMSPF;MaxFPS;MaxMSPF");
        app.SetReflectionBenchmarkTitle(name, durationInSeconds, 0.0f);
    }

    void ReflectionBenchmarkState::Exit()
    {
        fileQueueWriter.WriteAllLog();
        app.OnReflectionBenchmarkStateCompleted(isSsaaExpansionProbe, endsSsaaLevel,
                                                samplesTaken == 0 ? 0.0f : totalFps / samplesTaken);
        BenchmarkState::Exit();
    }

    bool ReflectionBenchmarkState::IsCompleted()
    {
        return samplesTaken >= durationInSeconds;
    }

    void ReflectionBenchmarkState::OnStatsCalculated(const TimeStats& stats)
    {
        ++samplesTaken;
        totalFps += stats.fps;
        Benchmark::PrintStatsCSV(stats, fileQueueWriter);

        const uint32_t remainingSeconds = durationInSeconds - std::min(samplesTaken, durationInSeconds);
        app.SetReflectionBenchmarkTitle(name, remainingSeconds, totalFps / samplesTaken);
    }
}
