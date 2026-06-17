#include "HybridGrassApp.h"
#include <cstring>
#include <exception>
using namespace Common;

int WINAPI WinMain(const HINSTANCE hInstance, HINSTANCE prevInstance,
                   PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        HybridGrassApp theApp(hInstance);
        if (cmdLine && strstr(cmdLine, "--perf-sweep") != nullptr)
        {
            theApp.EnablePerformanceSweepMode(4, 12);
        }
        else if (cmdLine && strstr(cmdLine, "--perf-test") != nullptr)
        {
            theApp.EnablePerformanceTestMode(5, 20);
        }
        if (!theApp.Initialize())
            return 0;

        auto result = theApp.Run();
        return result;
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
    catch (const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "Error", MB_OK);
        return 0;
    }
}
