#pragma once
#include <functional>
#include <string>

#include "BenchmarkState.h"
#include "Utils.h"


class FileQueueWriter;

class WaitState final : public BenchmarkState
{
public:
    WaitState(uint32_t timeInSeconds, const FileQueueWriter& writer, float timePerStep = 1.0f) : BenchmarkState(writer, timePerStep), waitTime(timeInSeconds)
    {
    }

    std::function<void(FileQueueWriter&)> OnEnter;
    std::function<void(FileQueueWriter&)> OnExit;
    std::function<void(FileQueueWriter&, const TimeStats&, float)> OnStatChanged;

private:
    void OnStatsCalculated(const TimeStats& stats) override;

    void Enter() override;

    void Exit() override;
    
    bool IsCompleted() override;
    uint32_t waitTime;
    uint32_t currentStatsCalculation = 0;
};
