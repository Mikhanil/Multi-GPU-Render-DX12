#include "GDeviceFactory.h"


#include "d3dUtil.h"
#include "GDevice.h"
#include "GCommandQueue.h"

namespace PEPEngine::Graphics
{
    ComPtr<IDXGIFactory4> GDeviceFactory::dxgiFactory = CreateFactory();
    Lazy<bool> GDeviceFactory::isTearingSupport = Lazy<bool>(CheckTearingSupport);
    std::vector<ComPtr<IDXGIAdapter3>> GDeviceFactory::adapters = GetAdapters();
    std::vector<std::shared_ptr<GDevice>> GDeviceFactory::hardwareDevices = CreateDevices();
    std::shared_ptr<GDevice> GDeviceFactory::wrapDevice = nullptr;

    ComPtr<IDXGIFactory4> GDeviceFactory::CreateFactory()
    {
#if defined(DEBUG) || defined(_DEBUG)
        {
            ComPtr<ID3D12Debug> debugController;
            ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
            debugController->EnableDebugLayer();

            ComPtr<ID3D12Debug1> spDebugController1;
            ThrowIfFailed(debugController->QueryInterface(IID_PPV_ARGS(&spDebugController1)));
            //spDebugController1->SetEnableGPUBasedValidation(true);
        }
#endif

        ComPtr<IDXGIFactory4> factory;

        UINT createFactoryFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
        createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

        ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&factory)));

        return factory;
    }

    std::vector<ComPtr<IDXGIAdapter3>> GDeviceFactory::GetAdapters()
    {
        std::vector<ComPtr<IDXGIAdapter3>> adapters;

        auto factory = GetFactory();

        UINT adapterindex = 0;
        ComPtr<IDXGIAdapter1> adapter;
        while (factory->EnumAdapters1(adapterindex++, &adapter) != DXGI_ERROR_NOT_FOUND)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                continue;
            }

            ComPtr<IDXGIAdapter3> adapter3;
            ThrowIfFailed(adapter->QueryInterface(IID_PPV_ARGS(&adapter3)));
            adapters.emplace_back((adapter3));
        }

        return adapters;
    }

    std::vector<std::shared_ptr<GDevice>> GDeviceFactory::CreateDevices()
    {
        std::vector<std::shared_ptr<GDevice>> devices;

        for (UINT i = 0; i < adapters.size(); ++i)
        {
            auto adapter = adapters[i];

            auto device = std::make_shared<GDevice>(adapter);
            device->Initialize();
            devices.emplace_back(device);
        }

        return devices;
    }

    bool GDeviceFactory::CheckTearingSupport()
    {
        BOOL allowTearing = FALSE;
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(dxgiFactory.As(&factory5)))
        {
            factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                          &allowTearing, sizeof(allowTearing));
        }

        return allowTearing == TRUE;
    }

    ComPtr<IDXGISwapChain4> GDeviceFactory::CreateSwapChain(const std::shared_ptr<GDevice>& device,
                                                            DXGI_SWAP_CHAIN_DESC1& desc, const HWND hwnd)
    {
        ComPtr<IDXGISwapChain4> swapChain;

        desc.Flags = IsTearingSupport()
                         ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
                         : DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        ComPtr<IDXGISwapChain1> swapChain1;
        ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(
            device->GetCommandQueue(GQueueType::Graphics)->GetD3D12CommandQueue().Get(),
            hwnd,
            &desc,
            nullptr,
            nullptr,
            &swapChain1));

        // Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
        // will be handled manually.
        ThrowIfFailed(dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

        ThrowIfFailed(swapChain1.As(&swapChain));

        return swapChain;
    }

    ComPtr<IDXGIFactory4> GDeviceFactory::GetFactory()
    {
        return dxgiFactory;
    }

    std::shared_ptr<GDevice> GDeviceFactory::GetDevice(const GraphicsAdapter adapter)
    {
        return hardwareDevices[adapter];
    }


    std::vector<std::shared_ptr<GDevice>> GDeviceFactory::GetAllDevices(const bool useWrap)
    {
        std::vector<std::shared_ptr<GDevice>> devices;

        devices.reserve(hardwareDevices.size());
        for (auto&& device : hardwareDevices)
        {
            devices.push_back(device);
        }

        if (!useWrap)
        {
            return devices;
        }

        if (wrapDevice == nullptr)
        {
            ComPtr<IDXGIAdapter1> adapter;
            {
                dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
                ComPtr<IDXGIAdapter3> adapter3;
                ThrowIfFailed(adapter->QueryInterface(IID_PPV_ARGS(&adapter3)));
                wrapDevice = (std::make_shared<GDevice>(adapter3));
                wrapDevice->Initialize();
            }
        }

        devices.push_back(wrapDevice);
        return devices;
    }

    bool GDeviceFactory::IsTearingSupport()
    {
        return isTearingSupport.value();
    }
}
