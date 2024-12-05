#include "MyWindow.h"
#include <winrt/base.h>
#include <filesystem>
#include <d3dcompiler.h>
#include <DirectXMath.h>

using namespace winrt;
using namespace std;
using namespace std::filesystem;
using namespace DirectX;

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT4 color;
};

path& GetBasePath()
{
    static optional<path> basePath;
    if (basePath) return *basePath;

    std::vector<wchar_t> buffer;
    buffer.resize(MAX_PATH);
    DWORD nSize = GetModuleFileName(nullptr, buffer.data(), (DWORD)buffer.size());
    path modulePath = buffer.data();

    basePath = modulePath.parent_path();
    return *basePath;
}

std::wstring GetAppPath(const std::wstring& relPath)
{
    return GetBasePath() / relPath;
}

MyWindow::MyWindow()
{
    aspectRatio = 1280.0f / 720.0f;
    viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, 1280.0f, 720.0f);
    scissorRect = CD3DX12_RECT(0, 0, 1280, 720);
}

void MyWindow::GetHardwareAdapter(IDXGIFactory1* pFactory, IDXGIAdapter1** ppAdapter, bool requestHighPerformanceAdapter)
{
    *ppAdapter = nullptr;

    com_ptr<IDXGIAdapter1> adapter;
    com_ptr<IDXGIFactory6> factory6;

    // factory6가 가능하다면 
    if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
    {
        DXGI_GPU_PREFERENCE preference = requestHighPerformanceAdapter ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED;

        for(UINT adapterIndex = 0; ;adapterIndex++)
        {
            if (FAILED(factory6->EnumAdapterByGpuPreference(adapterIndex, preference, IID_PPV_ARGS(&adapter)))) break;

            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            // software 렌더러인 경우 선택하지 않는다
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;

            // device를 만드는 것이 아니라, 그냥 Support 가능한지 확인만 한다
            if (SUCCEEDED(D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
                break;
        }
    }

    // adapter를 아직 못구했다면 전통적인 방식으로 구한다
    if (!adapter.get())
    {
        for(UINT adapterIndex = 0; ; ++adapterIndex)
        {
            if (FAILED(pFactory->EnumAdapters1(adapterIndex, adapter.put())))
                break;

            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;

            // device를 만드는 것이 아니라, 그냥 Support 가능한지 확인만 한다
            if (SUCCEEDED(D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
                break;
        }
    }

    *ppAdapter = adapter.detach();
}

bool MyWindow::LoadPipeline(HWND hWnd)
{
    UINT dxgiFactoryFlags = 0;

    // 1. 디버그 빌드일때 DebugLayer를 활성화한다
#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        com_ptr<ID3D12Debug> debugger;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugger))))
        {
            debugger->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    // 2. factory만들기
    com_ptr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory))))
        return false;

    // 3. hardware adapter만들기
    com_ptr<IDXGIAdapter1> hardwareAdapter;
    GetHardwareAdapter(factory.get(), hardwareAdapter.put());
    if (!hardwareAdapter) return false;

    // 4. device 만들기
    if (FAILED(D3D12CreateDevice(hardwareAdapter.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
        return false;

    // 5. CommandQueue만들기 Swapchain을 위해서 하나 만들어야 한다
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue))))
        return false;

    // 6. swap chain만들기, 윈도우 연결하기
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = 1280;
    swapChainDesc.Height = 720;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    com_ptr<IDXGISwapChain1> swapChain1;
    if (FAILED(factory->CreateSwapChainForHwnd(commandQueue.get(), hWnd, &swapChainDesc, nullptr, nullptr, swapChain1.put())))
        return false;
    
    if (FAILED(factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER)))
        return false;

    swapChain1.as(swapChain);
    if (!swapChain)
        return false;

    frameIndex = swapChain->GetCurrentBackBufferIndex();

    // 7. rtv용 descriptor heap만들기
    // Describe and create a render target view (RTV) descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap))))
        return false;

    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // 8. frame 리소스 만들기    
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());

    // Create a RTV for each frame.
    for (UINT n = 0; n < FrameCount; n++)
    {
        if (FAILED(swapChain->GetBuffer(n, IID_PPV_ARGS(&renderTargets[n]))))
            return false;

        // 렌더타겟뷰를 만든다
        device->CreateRenderTargetView(renderTargets[n].get(), nullptr, rtvHandle);

        rtvHandle.Offset(1, rtvDescriptorSize);
    }

    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator))))
        return false;

    return true;
}

bool MyWindow::LoadAssets()
{
    {
        // root signature는 무엇인가
        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init(/*numParameters*/ 0, /*pParameters*/ nullptr, /*numStaticSamplers*/ 0, /*pStaticSamplers*/ nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        com_ptr<ID3DBlob> signature;
        com_ptr<ID3DBlob> error;

        // signature를 데이터로 serialize시키고
        if (FAILED(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, signature.put(), error.put())))
            return false;

        // serialize된 데이터를 rootSignature로 만든다
        if (FAILED(device->CreateRootSignature(/*nodeMask*/0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature))))
            return false;
    }

    {
        // 파이프라인 스테이트 생성
        com_ptr<ID3DBlob> vertexShader;
        com_ptr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif
        if (FAILED(D3DCompileFromFile(GetAppPath(L"shaders.hlsl").c_str(), /*pDefines*/ nullptr, /*pInclude*/ nullptr, "VSMain", "vs_5_0", compileFlags, 0, vertexShader.put(), /*ppErrorMsgs*/ nullptr)))
            return false;

        if (FAILED(D3DCompileFromFile(GetAppPath(L"shaders.hlsl").c_str(), /*pDefines*/ nullptr, /*pInclude*/ nullptr, "PSMain", "ps_5_0", compileFlags, 0, pixelShader.put(), /*ppErrorMsgs*/ nullptr)))
            return false;

        // input layout 알려주기
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {   
            // SemanticName, SemanticIndex, Format, InputSlot, AlignedByteOffset, InputSlotClass, InstanceDataStepRate

            // POSITION은 ByteOffset이 0
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

            // COLOR는 ByteOffset이 12
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = rootSignature.get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;

        if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState))))
            return false;
    }

    // 1. Create the command list.
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.get(), nullptr, IID_PPV_ARGS(&commandList))))
        return false;

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    if (FAILED(commandList->Close()))
        return false;

    // create vertex buffer
    {
        Vertex triangleVertices[] =
        {
            {{ 0.0f, 0.25f * aspectRatio, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
            {{ 0.25f, -0.25f * aspectRatio, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
            {{ -0.25f, -0.25f * aspectRatio, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        };

        const UINT vertexBufferSize = sizeof(triangleVertices);

        // Note: using upload heaps to transfer static data like vert buffers is not 
        // recommended. Every time the GPU needs it, the upload heap will be marshalled 
        // over. Please read up on Default Heap usage. An upload heap is used here for 
        // code simplicity and because there are very few verts to actually transfer.
        if (FAILED(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&vertexBuffer))))
            return false;

        // Copy the triangle data to the vertex buffer.
        UINT8* pVertexDataBegin;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        if (FAILED(vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin))))
            return false;

        memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
        vertexBuffer->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
        vertexBufferView.StrideInBytes = sizeof(Vertex); // 하나씩 크기
        vertexBufferView.SizeInBytes = vertexBufferSize; // 총 크기
    }

    // Create synchronization objects.
    {
        // 2. fence 생성
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
            return false;

        fenceValue = 1;

        // Create an event handle to use for frame synchronization.

        // 3. event 만들기
        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (fenceEvent == nullptr)
        {
            if (FAILED(HRESULT_FROM_WIN32(GetLastError())))
                return false;
        }

        WaitForPreviousFrame();
    }

    return true;
}

bool MyWindow::PopulateCommandList()
{
    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.

    // 커맨드 리스트 할당자는 연관된 커맨드 리스트들이 GPU에서 모두 수행을 마쳐야만 리셋할수 있다.
    // 앱은 반드시 펜스를 사용해서 GPU 수행 여부를 알아내야 한다
    if (FAILED(commandAllocator->Reset()))
        return false;

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    if (FAILED(commandList->Reset(commandAllocator.get(), pipelineState.get())))
        return false;

    // Set necessary state.
    commandList->SetGraphicsRootSignature(rootSignature.get());
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);

    // Indicate that the back buffer will be used as a render target.
    auto transition0 = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIndex].get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(1, &transition0);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorSize);
    commandList->OMSetRenderTargets(1, &rtvHandle, /*RTsSingleHandleToDescriptorRange*/ FALSE, /*pDepthStencilDescriptor*/ nullptr);

    // Record commands.
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    commandList->DrawInstanced(3, 1, 0, 0);

    // Indicate that the back buffer will now be used to present.
    auto transition1 = CD3DX12_RESOURCE_BARRIER::Transition(renderTargets[frameIndex].get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    commandList->ResourceBarrier(1, &transition1);

    if (FAILED(commandList->Close()))
        return false;

    return true;
}

bool MyWindow::WaitForPreviousFrame()
{
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.     <- 기다리는건 좋은 방식이 아니다. 그냥 간단하게 하려고 이렇게 했다
    // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering <- D3D12HelloFrameBuffering에서
    // sample illustrates how to use fences for efficient resource usage and to
    // maximize GPU utilization.

    // Signal and increment the fence value.
    const UINT64 origFenceValue = fenceValue;
    if (FAILED(commandQueue->Signal(fence.get(), origFenceValue))) // 커맨드큐에 시그널을 넣는다. GPU가 실행하다가 fence를 트리거한다
        return false;

    fenceValue++;

    // Wait until the previous frame is finished.
    if (fence->GetCompletedValue() < origFenceValue) // 만약 origFenceValue보다 fence가 넘어섰다면 기다리지 않는다 (이미 끝났으니까)
    {
        // 그럼 여기 윗줄과 WaitForSingleObject Completion이 일어나면 어떻게 되는건가. 상관없나보다
        if (FAILED(fence->SetEventOnCompletion(origFenceValue, fenceEvent)))
            return false;

        // fenceEvent를 기다린다
        WaitForSingleObject(fenceEvent, INFINITE);
    }

    frameIndex = swapChain->GetCurrentBackBufferIndex();
    return true;
}

bool MyWindow::OnInit(HWND hWnd)
{
    if (!LoadPipeline(hWnd))
        return false;
    
    if (!LoadAssets())
        return false;

    return true;
}

void MyWindow::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForPreviousFrame();
    
    CloseHandle(fenceEvent);
}


void MyWindow::OnUpdate()
{
    // do nothing

}

bool MyWindow::OnRender()
{
    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { commandList.get() };
    commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    //// Present the frame.
    if (FAILED(swapChain->Present(1, 0)))
        return false;

    WaitForPreviousFrame();
    return true;
}

LRESULT CALLBACK MyWindow::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
        return 0;
    }

    case WM_PAINT:
        if (MyWindow* myWindow= reinterpret_cast<MyWindow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA)))
        {
            myWindow->OnUpdate();
            myWindow->OnRender();
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    // Handle any messages the switch statement didn't.
    return DefWindowProc(hWnd, message, wParam, lParam);
}
