#include "framework.h"
#include <cstring>
#include "DOFApp.h"

// Match any cmd-line argument whose substring contains "single" (case-insensitive)
// — covers --single-gpu, -single, /single-gpu, etc.
static bool ParseForceSingleGpu(LPSTR cmdLine)
{
    if (cmdLine == nullptr) return false;
    for (const char* p = cmdLine; *p; ++p)
    {
        if (_strnicmp(p, "single", 6) == 0) return true;
    }
    return false;
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine,
                   _In_ int nShowCmd)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    const bool forceSingle = ParseForceSingleGpu(lpCmdLine);

    try
    {
        DOFApp app(hInstance);
        app.SetForceSingleGpu(forceSingle);
        if (!app.Initialize())
            return 0;

        return app.Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}
