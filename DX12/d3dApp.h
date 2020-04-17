#pragma once

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "d3dUtil.h"
#include "GameTimer.h"

// Link necessary d3d12 libraries.
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

class D3DApp
{
protected:

    D3DApp(HINSTANCE hInstance);
    D3DApp(const D3DApp& rhs) = delete;
    D3DApp& operator=(const D3DApp& rhs) = delete;
    virtual ~D3DApp();

public:

    static D3DApp* GetApp();

    HINSTANCE AppInst()const;
    HWND      MainWnd()const;
    float     AspectRatio()const;

    bool Get4xMsaaState()const;
    void Set4xMsaaState(bool value);

    int Run();

    virtual bool Initialize();
    virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

protected:
    virtual void CreateRtvAndDsvDescriptorHeaps();
    virtual void OnResize();
    virtual void Update(const GameTimer& gt) = 0;
    virtual void Draw(const GameTimer& gt) = 0;
    virtual void OnKeyboardKeyUp(WPARAM key) {};
    virtual void OnKeyboardKeyDown(WPARAM key) {};
    virtual void OnMouseDown(WPARAM btnState, int x, int y) { }
    virtual void OnMouseUp(WPARAM btnState, int x, int y) { }
    virtual void OnMouseMove(WPARAM btnState, int x, int y) { }

protected:
    	
    std::wstring mainWindowCaption = L"d3d App";
    D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_HARDWARE;
    DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    int clientWidth = 800;
    int clientHeight = 600;
	
    static D3DApp* instance;
    HINSTANCE appInstance = nullptr;
    HWND      mainWindow = nullptr;
    bool      isAppPaused = false;
    bool      isMinimized = false;
    bool      isMaximized = false;
    bool      isResizing = false;
    bool      isFullscreen = false;
	
    // Set true to use 4X MSAA (§4.1.8).  The default is false.
    bool      isM4xMsaa = false;    // 4X MSAA enabled
    UINT      m4xMsaaQuality = 0;      // quality level of 4X MSAA

    GameTimer timer;
    bool InitMainWindow();

    Microsoft::WRL::ComPtr<IDXGIFactory4> dxgiFactory;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain;
    Microsoft::WRL::ComPtr<ID3D12Device> dxDevice;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    UINT64 currentFence = 0;
    bool InitDirect3D();

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> directCommandListAlloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;   
    void CreateCommandObjects();

    static const int swapChainBufferCount = 2;
    int currBackBufferIndex = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> swapChainBuffers[swapChainBufferCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilBuffer;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> renderTargetViewHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> depthStencilViewHeap;
    D3D12_VIEWPORT screenViewport;
    D3D12_RECT scissorRect;
    UINT rtvDescriptorSize = 0;
    UINT dsvDescriptorSize = 0;
    UINT cbvSrvUavDescriptorSize = 0;
    void CreateSwapChain();

    void FlushCommandQueue();

    ID3D12Resource* GetCurrentBackBuffer()const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackBufferView()const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView()const;

    void CalculateFrameStats();

    void LogAdapters();
    void LogAdapterOutputs(IDXGIAdapter* adapter);
    void LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format);
    void ExecuteCommandList() const;
    void ResetCommandList(ID3D12PipelineState* pipelinestate = nullptr) const;   
};

