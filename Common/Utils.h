#pragma once
#include <format>
#include <system_error>

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

    // File name: "<prefix><prime GPU name>+<second GPU name>.log" under BenchmarkLogs in the process working directory.
    inline std::filesystem::path GetLogFile(const std::wstring& name, const GDevice& primeDevice, const GDevice& secondDevice)
    {
        const std::wstring filename = name + primeDevice.GetName() + L"+" + secondDevice.GetName() + L".log";
        std::filesystem::path dir = std::filesystem::current_path() / L"BenchmarkLogs";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir / filename;
    }
}
