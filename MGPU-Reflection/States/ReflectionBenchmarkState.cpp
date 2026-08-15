#include "ReflectionBenchmarkState.h"

#include "../ReflectionApp.h"
#include "Utils.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

ReflectionBenchmarkState::ReflectionBenchmarkState(HybridCubeMapApp& app,
                                                   const ReflectionProbeConfiguration configuration,
                                                   std::wstring name,
                                                   const uint32_t stateIndex,
                                                   const uint32_t stateCount,
                                                   const uint32_t durationInSeconds, const FileQueueWriter& writer)
    : BenchmarkState(writer), app(app), configuration(configuration), name(std::move(name)),
      stateIndex(stateIndex), stateCount(stateCount), durationInSeconds(durationInSeconds)
{
}

void ReflectionBenchmarkState::Enter()
{
    BenchmarkState::Enter();
    settleElapsed = 0.0f;
    samplesTaken = 0;
    displayedSettleSeconds = static_cast<uint32_t>(std::ceil(SettleSeconds));
    sampling = false;
    app.SetReflectionBenchmarkConfiguration(configuration);
    app.UpdateReflectionBenchmarkStatus(name, stateIndex, stateCount,
                                        displayedSettleSeconds, nullptr, true);
}

void ReflectionBenchmarkState::Tick(const float deltaTime)
{
    if (!sampling)
    {
        settleElapsed += deltaTime;
        const auto remainingSettleSeconds = static_cast<uint32_t>(
            std::ceil(std::max(0.0f, SettleSeconds - settleElapsed)));
        if (remainingSettleSeconds != displayedSettleSeconds)
        {
            displayedSettleSeconds = remainingSettleSeconds;
            app.UpdateReflectionBenchmarkStatus(name, stateIndex, stateCount,
                                                displayedSettleSeconds, nullptr, true);
        }
        if (settleElapsed < SettleSeconds)
        {
            return;
        }

        // Drop all prewarm frames and start the framework's one-second samples
        // from a clean counter once the static bake and fixed camera settle.
        BenchmarkState::Enter();
        sampling = true;
        fileQueueWriter.PushMessage(L"FPS;MSPF;MinFPS;MinMSPF;MaxFPS;MaxMSPF");
        app.UpdateReflectionBenchmarkStatus(name, stateIndex, stateCount,
                                            durationInSeconds, nullptr, false);
    }

    BenchmarkState::Tick(deltaTime);
}

void ReflectionBenchmarkState::Exit()
{
    if (!fileQueueWriter.WriteAllLog())
    {
        OutputDebugStringW(L"MGPU-Reflection failed to write benchmark metrics.\n");
        MessageBoxW(nullptr, L"MGPU-Reflection failed to write benchmark metrics.",
                    L"MGPU-Reflection benchmark error", MB_OK | MB_ICONERROR);
        std::exit(EXIT_FAILURE);
    }
    BenchmarkState::Exit();
}

bool ReflectionBenchmarkState::IsCompleted()
{
    return sampling && samplesTaken >= durationInSeconds;
}

void ReflectionBenchmarkState::OnStatsCalculated(const TimeStats& stats)
{
    ++samplesTaken;
    Benchmark::PrintStatsCSV(stats, fileQueueWriter);
    const uint32_t remainingSeconds = durationInSeconds - std::min(samplesTaken, durationInSeconds);
    app.UpdateReflectionBenchmarkStatus(name, stateIndex, stateCount,
                                        remainingSeconds, &stats, false);
}
