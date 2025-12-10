#include "pch.h"
#include "WaitState.h"

void WaitState::OnStatsCalculated(const TimeStats& stats)
{
    currentStatsCalculation++;
    OnStatChanged(this->fileQueueWriter, stats, static_cast<float>(currentStatsCalculation) / waitTime);
}

void WaitState::Enter()
{
    BenchmarkState::Enter();
    OnEnter(fileQueueWriter);
}

void WaitState::Exit()
{
    BenchmarkState::Exit();
    OnExit(fileQueueWriter);
}

bool WaitState::IsCompleted()
{
    return currentStatsCalculation >= waitTime;
}
