#include "ReflectionBenchmarkState.h"

#include "../ReflectionApp.h"
#include "Utils.h"
#include <algorithm>

ReflectionBenchmarkState::ReflectionBenchmarkState(HybridCubeMapApp& app, const bool useOnlyPrime,
                                                   std::wstring name, const uint32_t durationInSeconds,
                                                   const FileQueueWriter& writer)
    : BenchmarkState(writer), app(app), useOnlyPrime(useOnlyPrime), name(std::move(name)),
      durationInSeconds(durationInSeconds)
{
}

void ReflectionBenchmarkState::Enter()
{
    BenchmarkState::Enter();
    settleElapsed = 0.0f;
    samplesTaken = 0;
    totalFps = 0.0f;
    sampling = false;
    app.SetReflectionBenchmarkConfiguration(useOnlyPrime);
    app.SetReflectionBenchmarkTitle(name, durationInSeconds, 0.0f, true);
}

void ReflectionBenchmarkState::Tick(const float deltaTime)
{
    if (!sampling)
    {
        settleElapsed += deltaTime;
        if (settleElapsed < SettleSeconds)
        {
            return;
        }

        // Drop all prewarm frames and start the framework's one-second samples
        // from a clean counter once the static bake and fixed camera settle.
        BenchmarkState::Enter();
        sampling = true;
        fileQueueWriter.PushMessage(L"FPS;MSPF;MinFPS;MinMSPF;MaxFPS;MaxMSPF");
        app.SetReflectionBenchmarkTitle(name, durationInSeconds, 0.0f, false);
    }

    BenchmarkState::Tick(deltaTime);
}

void ReflectionBenchmarkState::Exit()
{
    fileQueueWriter.WriteAllLog();
    BenchmarkState::Exit();
}

bool ReflectionBenchmarkState::IsCompleted()
{
    return sampling && samplesTaken >= durationInSeconds;
}

void ReflectionBenchmarkState::OnStatsCalculated(const TimeStats& stats)
{
    ++samplesTaken;
    totalFps += stats.fps;
    Benchmark::PrintStatsCSV(stats, fileQueueWriter);
    const uint32_t remainingSeconds = durationInSeconds - std::min(samplesTaken, durationInSeconds);
    app.SetReflectionBenchmarkTitle(name, remainingSeconds, totalFps / samplesTaken, false);
}
