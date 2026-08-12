#pragma once
#include <wrl/client.h>
#include  "dxgi1_6.h"
#include "MemoryAllocator.h"
#include "Lazy.h"

using namespace Microsoft::WRL;

namespace PEPEngine::Graphics
{
    using namespace Allocator;
    using namespace Utils;

    enum GraphicsAdapter : UINT
    {
        GraphicAdapterPrimary = 0,
        GraphicAdapterSecond = GraphicAdapterPrimary + 1,
        GraphicAdapterThird = GraphicAdapterSecond + 1,
        GraphicAdapterCount = GraphicAdapterThird + 1
    };

    class GDevice;

    class GDeviceFactory
    {
        static ComPtr<IDXGIFactory4> CreateFactory();
        static ComPtr<IDXGIFactory4> dxgiFactory;

        static bool CheckTearingSupport();
        static Lazy<bool> isTearingSupport;

        static std::vector<std::shared_ptr<GDevice>> CreateDevices();
        static std::vector<std::shared_ptr<GDevice>> hardwareDevices;
        static std::shared_ptr<GDevice> wrapDevice;
        static std::vector<std::shared_ptr<GDevice>> devicesWithWarp;

        static std::vector<ComPtr<IDXGIAdapter3>> GetAdapters();
        static std::vector<ComPtr<IDXGIAdapter3>> adapters;

    public:
        static ComPtr<IDXGISwapChain4> CreateSwapChain(const std::shared_ptr<GDevice>& device,
                                                       DXGI_SWAP_CHAIN_DESC1& desc,
                                                       HWND hwnd);

        static ComPtr<IDXGIFactory4> GetFactory();

        // Device selection is index-based so callers are not limited by the
        // legacy two-adapter enum. The enum names remain for older samples.
        static std::shared_ptr<GDevice>& GetDevice(UINT adapterIndex = GraphicAdapterPrimary);

        static std::vector<std::shared_ptr<GDevice>>& GetAllDevices(bool useWrap = false);

        static bool IsTearingSupport();
    };
}
