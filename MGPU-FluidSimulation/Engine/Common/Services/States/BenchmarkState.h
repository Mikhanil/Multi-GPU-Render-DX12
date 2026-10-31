#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <../Common/Services/FileQueueWriter.h>


struct TimeStats
{
    uint64_t frameNumber = 0;
    float fps;
    float mspf;
    float minFps = std::numeric_limits<float>::max();
    float minMspf = std::numeric_limits<float>::max();
    float maxFps = std::numeric_limits<float>::min();
    float maxMspf = std::numeric_limits<float>::min();
};


class BenchmarkState
{
    TimeStats stats;

protected:
    FileQueueWriter fileQueueWriter;

public:
    virtual ~BenchmarkState() = default;


    virtual void Enter()
    {
        tickCount = 0;
        timeElapsed = 0;
    }

    virtual void Tick(float deltaTime)
    {
        tickCount++;
        timeElapsed += deltaTime;
        if (NeedCalculateStats())
        {
            CalculateTimeStats();
        }
    }

    virtual void Exit()
    {
        tickCount = 0;
        timeElapsed = 0;
    }

    virtual bool IsCompleted() = 0;

protected:
    virtual void OnStatsCalculated(const TimeStats& calculatedStats)
    {
    }

    BenchmarkState(const FileQueueWriter& writer, float timePerStep = 1.0f): stats(), fileQueueWriter(writer), timeElapsed(0), timePerStatCalculation(timePerStep)
    {
    }

    bool NeedCalculateStats() const
    {
        return timeElapsed >= timePerStatCalculation;
    }

    void CalculateTimeStats()
    {
        float fps = static_cast<float>(tickCount); // fps = frameCnt / 1
        float mspf = 1000.0f / fps;

        stats.frameNumber += tickCount;
        stats.fps = fps;
        stats.mspf = mspf;
        stats.minFps = std::min(fps, stats.minFps);
        stats.minMspf = std::min(mspf, stats.minMspf);
        stats.maxFps = std::max(fps, stats.maxFps);
        stats.maxMspf = std::max(mspf, stats.maxMspf);

        OnStatsCalculated(stats);

        tickCount = 0;
        timeElapsed = 0;
    }

    float timeElapsed;
    uint64_t tickCount = 0;

    float timePerStatCalculation = 1.0f;
};
