#include <d3d11.h>
#include <d3dcompiler.h>
#include <Windows.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

typedef void (*CheckMyEngineDll)(HWND, ID3D11Device*, ID3D11DeviceContext*);
typedef void (*RenderEngineDll)(ID3D11DeviceContext*, ID3D11Device*, HWND);
typedef LRESULT(*ImguiWndProcDll)(HWND, UINT, WPARAM, LPARAM);
typedef void (*ImguiUIEngineDll)(HWND);

RenderEngineDll RenderEngine = nullptr;
ImguiUIEngineDll ImguiUIEngine = nullptr;
CheckMyEngineDll CheckMyEngine = nullptr;
ImguiWndProcDll ImguiWndProc = nullptr;

HWND hwnd;
IDXGISwapChain* SwapChain = nullptr;
ID3D11Device* Device = nullptr;
ID3D11DeviceContext* Context = nullptr;
ID3D11RenderTargetView* rtv;
ID3D11Texture2D* depthBuffer;
ID3D11DepthStencilView* dsv;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (ImguiWndProc && ImguiWndProc(hwnd, msg, wParam, lParam)) return true;

	switch (msg) {
	case WM_SIZE: // ウィンドウサイズが変わったとき（最大化ボタン含む）
		if (Device != nullptr && wParam != SIZE_MINIMIZED) {
			int width = LOWORD(lParam);
			int height = HIWORD(lParam);

			// 1. 古いビューを解放（これをしないとResizeBuffersは失敗する）
			if (rtv) { rtv->Release(); rtv = nullptr; }
			if (dsv) { dsv->Release(); dsv = nullptr; }

			// 2. バックバッファのサイズ変更
			SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

			// 3. RenderTargetViewの再作成
			ID3D11Texture2D* pBackBuffer = nullptr;
			SwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
			if (pBackBuffer) {
				Device->CreateRenderTargetView(pBackBuffer, NULL, &rtv);
				pBackBuffer->Release();
			}

			// 4. DepthStencilViewの再作成
			D3D11_TEXTURE2D_DESC descDepth = {};
			descDepth.Width = width;
			descDepth.Height = height;
			descDepth.MipLevels = 1;
			descDepth.ArraySize = 1;
			descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			descDepth.SampleDesc.Count = 1;
			descDepth.Usage = D3D11_USAGE_DEFAULT;
			descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;

			ID3D11Texture2D* pDepthStencil = nullptr;
			Device->CreateTexture2D(&descDepth, NULL, &pDepthStencil);
			if (pDepthStencil) {
				Device->CreateDepthStencilView(pDepthStencil, NULL, &dsv);
				pDepthStencil->Release();
			}
		}
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int mCmdShow) {
	WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L,
	GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"DX11EG", NULL};
	RegisterClassEx(&wc);
	hwnd = CreateWindow(wc.lpszClassName, L"DX11_GameEngine", WS_OVERLAPPEDWINDOW,
		100, 100, 1600, 1000, NULL, NULL, wc.hInstance, NULL);
	
	DXGI_SWAP_CHAIN_DESC scd = {};
	scd.BufferCount = 2;
	scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scd.OutputWindow = hwnd;
	scd.SampleDesc.Count = 1;
	scd.Windowed = TRUE;
	scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE,
		nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &scd, &SwapChain, &Device, nullptr, &Context);
	if (FAILED(hr)) {
		MessageBox(NULL, L"D3D11CreateDeviceAndSwapChain Error", L"ERROR", MB_ICONERROR);
	}

	ID3D11Texture2D* BackBuffer;
	SwapChain->GetBuffer(0, IID_PPV_ARGS(&BackBuffer));
	Device->CreateRenderTargetView(BackBuffer, NULL, &rtv);
	BackBuffer->Release();

	D3D11_TEXTURE2D_DESC dbd = {};
	dbd.Width = 1600;
	dbd.Height = 1000;
	dbd.MipLevels = 1;
	dbd.ArraySize = 1;
	dbd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dbd.SampleDesc.Count = 1;
	dbd.Usage = D3D11_USAGE_DEFAULT;
	dbd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	Device->CreateTexture2D(&dbd, NULL, &depthBuffer);
	Device->CreateDepthStencilView(depthBuffer, NULL, &dsv);
	depthBuffer->Release();

	HMODULE LoadEnginedll = LoadLibrary(L"Engine.dll");
	// ... WinMain内 ...
	if (LoadEnginedll) {
		CheckMyEngine = (CheckMyEngineDll)GetProcAddress(LoadEnginedll, "CheckEngine");
		RenderEngine = (RenderEngineDll)GetProcAddress(LoadEnginedll, "RenderEngine");
		ImguiUIEngine = (ImguiUIEngineDll)GetProcAddress(LoadEnginedll, "ImguiUIEngine");
		ImguiWndProc = (ImguiWndProcDll)GetProcAddress(LoadEnginedll, "ImguiWndProc_DLL");

		if (CheckMyEngine) CheckMyEngine(hwnd, Device, Context);
	}

	ShowWindow(hwnd, SW_SHOWMAXIMIZED);

	MSG msg = { 0 };
	while (msg.message != WM_QUIT) {
		if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}
		// While loop
		float clear_color[4] = { 0.45f, 0.55f, 0.60f, 1.00f };
		Context->ClearRenderTargetView(rtv, clear_color);
		Context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
		Context->OMSetRenderTargets(1, &rtv, dsv);

		if (RenderEngine) RenderEngine(Context, Device, hwnd);
		if (ImguiUIEngine) ImguiUIEngine(hwnd);
		
		SwapChain->Present(1, 0);
	}

	SwapChain->Release();
	Device->Release();
	Context->Release();
	rtv->Release();

	return 0;
}