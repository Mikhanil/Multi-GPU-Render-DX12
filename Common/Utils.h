#pragma once
#include <format>

#include "Services/FileQueueWriter.h"
#include "Services/States/BenchmarkState.h"

class FileQueueWriter;

namespace Benchmark
{
    inline static void PrintStatsCSV(const TimeStats& stats, FileQueueWriter& logs)
    {
        const std::wstring staticticStr = std::format(L"{:.2f};{:.2f};{:.2f};{:.2f};{:.2f};{:.2f}",
            stats.fps, stats.mspf, stats.minFps, stats.minMspf, stats.maxFps, stats.maxMspf);
        logs.PushMessage(staticticStr);
    }

    inline std::filesystem::path GetLogFile(const std::wstring& name, const GDevice& primeDevice, const GDevice& secondDevice)
    {
        const std::filesystem::path filePath(
           name + primeDevice.GetName() + L"+" + secondDevice.GetName() + L".log");
        const auto path = std::filesystem::current_path().wstring() + L"\\" + filePath.wstring();
        return path;
    }
}
