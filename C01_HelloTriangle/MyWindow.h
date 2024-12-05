#pragma once
#include <Windows.h>
#include <winrt/base.h>
#include <directx/d3d12.h>
#include <directx/d3dx12.h>
#include <dxgi1_6.h>

class MyWindow
{    
    static const UINT FrameCount = 2;

    winrt::com_ptr<ID3D12Device> device;
    winrt::com_ptr<ID3D12CommandQueue> commandQueue;
    winrt::com_ptr<IDXGISwapChain3> swapChain;
    winrt::com_ptr<ID3D12DescriptorHeap> rtvHeap;
    winrt::com_ptr<ID3D12Resource> renderTargets[FrameCount];
    winrt::com_ptr<ID3D12CommandAllocator> commandAllocator;

    // LoadAssets에서 만듦
    winrt::com_ptr<ID3D12GraphicsCommandList> commandList;
    winrt::com_ptr<ID3D12Fence> fence;
    winrt::com_ptr<ID3D12PipelineState> pipelineState; // 이걸 초기화하지 않는다.

    winrt::com_ptr<ID3D12RootSignature> rootSignature;
    winrt::com_ptr<ID3D12Resource> vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

    HANDLE fenceEvent;

    UINT frameIndex;
    UINT rtvDescriptorSize;
    UINT fenceValue;

    FLOAT aspectRatio;
    CD3DX12_VIEWPORT viewport;
    CD3DX12_RECT scissorRect;

public:
    MyWindow();

private:
    void GetHardwareAdapter(IDXGIFactory1* pFactory, IDXGIAdapter1** ppAdapter, bool requestHighPerformanceAdapter = false);

    bool LoadPipeline(HWND hWnd);
    bool LoadAssets();
    bool PopulateCommandList();
    bool WaitForPreviousFrame();

public:
    bool OnInit(HWND hWnd);
    void OnDestroy();

private:
    void OnUpdate();
    bool OnRender();

public:
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
};
