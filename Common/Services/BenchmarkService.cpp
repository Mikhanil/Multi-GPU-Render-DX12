#include "pch.h"
#include "BenchmarkService.h"
#include "FileQueueWriter.h"


void BenchmarkService::SetState(BenchmarkState* state)
{
    if (CurrentState != nullptr)
    {
        CurrentState->Exit();
    }
    CurrentState = state;
    if (CurrentState != nullptr)
    {
        CurrentState->Enter();
    }
}

void BenchmarkService::Start()
{
    currentStateIndex = 0;
    if (!states.empty())
    {
        SetState(states[currentStateIndex].get());
    }
}


void BenchmarkService::Tick(float deltaTime)
{
    if (currentStateIndex < 0 || currentStateIndex >= states.size()) return;
    if (CurrentState)
    {
        CurrentState->Tick(deltaTime);
        if (CurrentState->IsCompleted())
        {
            const int nextStateIndex = currentStateIndex + 1;
            if (nextStateIndex < static_cast<int>(states.size()))
            {
                currentStateIndex = nextStateIndex;
                SetState(states[currentStateIndex].get());
            }
            else if (looping && !states.empty())
            {
                currentStateIndex = 0;
                SetState(states[currentStateIndex].get());
            }
            else
            {
                SetState(nullptr);
                currentStateIndex = -1;
            }
        }
    }
}
