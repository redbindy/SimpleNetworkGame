#include "App.h"

#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include <dxgi.h>
#pragma comment(lib, "dxgi.lib")

#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

#include <DirectXTex.h>
#pragma comment(lib, "DirectXTex.lib")

#include <WS2tcpip.h>

#include <DirectXMath.h>
#include <cassert>

#include "WICTextureLoader.h"

LRESULT CALLBACK WndProc(
	const HWND hWnd,
	const UINT message,
	const WPARAM wParam,
	const LPARAM lParam
);

enum
{
	DEFAULT_WIDTH = 1280,
	DEFAULT_HEIGHT = 960
};

using namespace DirectX;
struct BgVertex
{
	XMFLOAT3 pos;
	XMFLOAT2 uv;
};

struct ConstantBuffer
{
	XMFLOAT2 pos;
	XMFLOAT2 dummy1;
};
static_assert(sizeof(ConstantBuffer) % 16 == 0, "");

// 윈도우 관련
static const TCHAR* CLASS_NAME = TEXT("SimpleNetworkGame");
static HINSTANCE shInstance = nullptr;
static HWND shWnd = nullptr;

// d3d
static ID3D11Device* spDevice = nullptr;
static ID3D11DeviceContext* spContext = nullptr;
static IDXGISwapChain* spSwapChain = nullptr;

static ID3D11VertexShader* spVS = nullptr;

static ConstantBuffer sPlayer1PosBufferCPU;
static ConstantBuffer sPlayer2PosBufferCPU;
static ID3D11Buffer* spPlayer1PosBufferGPU = nullptr;
static ID3D11Buffer* spPlayer2PosBufferGPU = nullptr;

static ID3D11PixelShader* spPS = nullptr;

static constexpr int BG_VERTEX_COUNT = 4;
static ID3D11Buffer* spBgVertexBuffer = nullptr;

static ID3D11Texture2D* spBgTexture = nullptr;
static ID3D11ShaderResourceView* spBgTextureView = nullptr;
static ID3D11SamplerState* spSampler = nullptr;

static D3D11_VIEWPORT sViewport;

static ID3D11RenderTargetView* spRTV = nullptr;

// 소켓
static SOCKET sSock;

static HANDLE shPeerDataThread;
static DWORD sPeerDataThreadID;

static void updateMyData();
static DWORD WINAPI updatePeerData(const LPVOID lpParam);

void App::Initialize()
{
	// 윈도우 초기화
	{
		shInstance = static_cast<HINSTANCE>(GetModuleHandle(nullptr));

		WNDCLASSEX windowClass;
		ZeroMemory(&windowClass, sizeof(windowClass));

		windowClass.cbSize = sizeof(windowClass);
		windowClass.lpfnWndProc = WndProc;
		windowClass.lpszClassName = CLASS_NAME;
		windowClass.hCursor = (HCURSOR)LoadCursor(nullptr, IDC_ARROW);
		windowClass.hInstance = shInstance;

		DWORD errorCode = GetLastError();
		RegisterClassEx(&windowClass);
		ASSERT(errorCode == ERROR_SUCCESS, "RegisterClass failed");

		shWnd = CreateWindowEx(
			0,
			CLASS_NAME,
			CLASS_NAME,
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT,
			DEFAULT_WIDTH, DEFAULT_HEIGHT,
			nullptr,
			nullptr,
			shInstance,
			nullptr
		);

		errorCode = GetLastError();
		ASSERT(errorCode == ERROR_SUCCESS, "CreateWindow failed");

		errorCode = GetLastError();
		ShowWindow(shWnd, SW_SHOWNORMAL);
		ASSERT(errorCode == ERROR_SUCCESS, "ShowWindow failed");

		std::cout << "Window Creation Success" << std::endl;

#if SERVER
		SetWindowText(shWnd, TEXT("Server"));
#else
		SetWindowText(shWnd, TEXT("Client"));
#endif
	}

	// D3D 초기화
	{
		DXGI_SWAP_CHAIN_DESC swapChainDesc;
		ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));

		swapChainDesc.BufferCount = 3;
		swapChainDesc.BufferDesc.Width = DEFAULT_WIDTH;
		swapChainDesc.BufferDesc.Height = DEFAULT_HEIGHT;
		swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.BufferDesc.RefreshRate.Denominator = 60;
		swapChainDesc.BufferDesc.RefreshRate.Numerator = 1;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.OutputWindow = shWnd;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.Windowed = true;

		UINT creationFlags = 0;
#if defined(_DEBUG) || defined(DEBUG)
		creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

		const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

		HRESULT hr = D3D11CreateDeviceAndSwapChain(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			creationFlags,
			&featureLevel,
			1,
			D3D11_SDK_VERSION,
			&swapChainDesc,
			&spSwapChain,
			&spDevice,
			nullptr,
			&spContext
		);
		ASSERT(SUCCEEDED(hr), "CreateDeviceAndSwapChain failed");

		ID3DBlob* pShaderBlob = nullptr;
		ID3DBlob* pErrorMsg = nullptr;
		{
			hr = D3DCompileFromFile(
				TEXT("VS.hlsl"),
				nullptr,
				D3D_COMPILE_STANDARD_FILE_INCLUDE,
				"main",
				"vs_5_0",
				D3DCOMPILE_DEBUG,
				0,
				&pShaderBlob,
				&pErrorMsg
			);
			ASSERT(SUCCEEDED(hr), "Compiling vertex shader failed");

			hr = spDevice->CreateVertexShader(
				pShaderBlob->GetBufferPointer(),
				pShaderBlob->GetBufferSize(),
				nullptr,
				&spVS
			);
			ASSERT(SUCCEEDED(hr), "CreateVertexShader failed");

			constexpr int NUM_ELEMENTS = 2;
			D3D11_INPUT_ELEMENT_DESC inputElements[NUM_ELEMENTS] = {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, sizeof(BgVertex::pos), D3D11_INPUT_PER_VERTEX_DATA, 0},
			};

			ID3D11InputLayout* pInputLayout = nullptr;
			{
				hr = spDevice->CreateInputLayout(
					inputElements,
					NUM_ELEMENTS,
					pShaderBlob->GetBufferPointer(),
					pShaderBlob->GetBufferSize(),
					&pInputLayout
				);
				ASSERT(SUCCEEDED(hr), "CreateInputLayout failed");

				spContext->IASetInputLayout(pInputLayout);
			}
			ReleaseCOM(pInputLayout);
			ReleaseCOM(pShaderBlob);

			hr = D3DCompileFromFile(
				TEXT("BgPS.hlsl"),
				nullptr,
				D3D_COMPILE_STANDARD_FILE_INCLUDE,
				"main",
				"ps_5_0",
				D3DCOMPILE_DEBUG,
				0,
				&pShaderBlob,
				&pErrorMsg
			);
			ASSERT(SUCCEEDED(hr), "Compiling pixel shader failed");

			hr = spDevice->CreatePixelShader(
				pShaderBlob->GetBufferPointer(),
				pShaderBlob->GetBufferSize(),
				nullptr,
				&spPS
			);
			ASSERT(SUCCEEDED(hr), "CreatePixelShader failed");
		}
		ReleaseCOM(pShaderBlob);
		ReleaseCOM(pErrorMsg);

		BgVertex bgVertices[BG_VERTEX_COUNT] = {
			{ XMFLOAT3(-1.f, 1.f, 0.f), XMFLOAT2(0.f, 0.f) },
			{ XMFLOAT3(1.f, 1.f, 0.f), XMFLOAT2(1.f, 0.f) },
			{ XMFLOAT3(-1.f, -1.f, 0.f), XMFLOAT2(0.f, 1.f) },
			{ XMFLOAT3(1.f, -1.f, 0.f), XMFLOAT2(1.f, 1.f) },
		};

		D3D11_BUFFER_DESC bufferDesc;
		ZeroMemory(&bufferDesc, sizeof(bufferDesc));

		bufferDesc.Usage = D3D11_USAGE_DEFAULT;
		bufferDesc.ByteWidth = sizeof(bgVertices);
		bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.StructureByteStride = sizeof(BgVertex);

		D3D11_SUBRESOURCE_DATA initData;
		ZeroMemory(&initData, sizeof(initData));

		initData.pSysMem = bgVertices;

		hr = spDevice->CreateBuffer(&bufferDesc, &initData, &spBgVertexBuffer);
		ASSERT(SUCCEEDED(hr), "CreateBuffer for bgVertices failed");

		sPlayer1PosBufferCPU.pos = { -0.05f, 0.f };
		sPlayer2PosBufferCPU.pos = { 0.05f, 0.f };

		bufferDesc.Usage = D3D11_USAGE_DEFAULT;
		bufferDesc.ByteWidth = sizeof(ConstantBuffer);
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bufferDesc.StructureByteStride = sizeof(XMFLOAT2);

		initData.pSysMem = &sPlayer1PosBufferCPU;

		hr = spDevice->CreateBuffer(&bufferDesc, &initData, &spPlayer1PosBufferGPU);
		ASSERT(SUCCEEDED(hr), "CreateBuffer for player1Pos failed");

		initData.pSysMem = &sPlayer2PosBufferCPU;

		hr = spDevice->CreateBuffer(&bufferDesc, &initData, &spPlayer2PosBufferGPU);
		ASSERT(SUCCEEDED(hr), "CreateBuffer for player2Pos failed");

		sViewport.TopLeftX = 0.f;
		sViewport.TopLeftY = 0.f;
		sViewport.Width = DEFAULT_WIDTH;
		sViewport.Height = DEFAULT_HEIGHT;
		sViewport.MinDepth = 0.f;
		sViewport.MaxDepth = 1.f;

		assert(spDevice != nullptr);
		hr = CreateWICTextureFromFile(
			spDevice,
			spContext,
			TEXT("SNES - The Legend of Zelda A Link to the Past - Light World.png"),
			nullptr,
			&spBgTextureView
		);
		ASSERT(SUCCEEDED(hr), "CreateBgTexture failed");

		D3D11_SAMPLER_DESC samplerDesc;
		ZeroMemory(&samplerDesc, sizeof(samplerDesc));

		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

		hr = spDevice->CreateSamplerState(&samplerDesc, &spSampler);
		ASSERT(SUCCEEDED(hr), "CreateSamplerState failed");

		ID3D11Texture2D* pBackBuffer = nullptr;
		hr = spSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
		ASSERT(SUCCEEDED(hr), "GetBuffer for backBuffer failed");

		assert(pBackBuffer != nullptr);
		hr = spDevice->CreateRenderTargetView(
			pBackBuffer,
			nullptr,
			&spRTV
		);
		ReleaseCOM(pBackBuffer);

		spContext->OMSetRenderTargets(1, &spRTV, nullptr);

		std::cout << "D3D init Success" << std::endl;
	}

	//소켓
	{
		enum
		{
			PORT = 25565
		};

		WSADATA wsaData;

		int errorCode = WSAStartup(MAKEWORD(2, 2), &wsaData);
		ASSERT(errorCode == ERROR_SUCCESS, "WSAStartup failed");

#if SERVER
		SOCKET listeningSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listeningSock == INVALID_SOCKET)
		{
			ASSERT(false, "socket failed");

			WSACleanup();

			return;
		}

		sockaddr_in hint;
		ZeroMemory(&hint, sizeof(hint));

		hint.sin_family = AF_INET;
		hint.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
		hint.sin_port = htons(PORT);

		errorCode = bind(listeningSock, reinterpret_cast<sockaddr*>(&hint), sizeof(hint));
		if (errorCode == SOCKET_ERROR)
		{
			ASSERT(false, "bind failed");

			closesocket(listeningSock);
			WSACleanup();

			return;
		}

		errorCode = listen(listeningSock, SOMAXCONN);
		if (errorCode == SOCKET_ERROR)
		{
			ASSERT(false, "listen failed");

			closesocket(listeningSock);
			WSACleanup();

			return;
		}

		sockaddr_in clientSockInfo;
		int clientSize = sizeof(clientSockInfo);

		sSock = accept(listeningSock, reinterpret_cast<sockaddr*>(&clientSockInfo), &clientSize);
		if (sSock == INVALID_SOCKET)
		{
			ASSERT(false, "accept failed");

			closesocket(listeningSock);
			WSACleanup();

			return;
		}

		closesocket(listeningSock);
#else
		sSock = socket(AF_INET, SOCK_STREAM, 0);
		if (sSock == INVALID_SOCKET)
		{
			ASSERT(false, "socket failed");

			WSACleanup();

			return;
		}

		sockaddr_in hint;
		ZeroMemory(&hint, sizeof(hint));

		hint.sin_family = AF_INET;
		hint.sin_port = htons(PORT);

		const char* SERVER_IP = "127.0.0.1";

		errorCode = inet_pton(AF_INET, SERVER_IP, &hint.sin_addr);

		if (errorCode != 1)
		{
			ASSERT(false, "inet_pton failed");

			WSACleanup();

			return;
		}

		errorCode = connect(sSock, reinterpret_cast<sockaddr*>(&hint), sizeof(hint));
		if (errorCode == SOCKET_ERROR)
		{
			ASSERT(false, "connect fialed");

			closesocket(sSock);
			WSACleanup();

			return;
		}
#endif

#if 0
		DWORD recvTimeout = 17;
		setsockopt(sSock, SOL_SOCKET, SO_RCVTIMEO, (char*)&recvTimeout, sizeof(recvTimeout));
#endif
	}

	// 스레드
	{
		shPeerDataThread = CreateThread(
			nullptr,
			0,
			updatePeerData,
			nullptr,
			0,
			&sPeerDataThreadID
		);

		DWORD errorCode = GetLastError();
		ASSERT(errorCode == ERROR_SUCCESS, "CreateThread failed");
	}
}

void App::Destroy()
{
	CloseHandle(shPeerDataThread);

	closesocket(sSock);
	WSACleanup();

	ReleaseCOM(spRTV);
	ReleaseCOM(spBgTextureView);
	ReleaseCOM(spBgTexture);
	ReleaseCOM(spBgVertexBuffer);
	ReleaseCOM(spPlayer1PosBufferGPU);
	ReleaseCOM(spVS);
	ReleaseCOM(spPS);
	ReleaseCOM(spSwapChain);
	ReleaseCOM(spContext);
	ReleaseCOM(spDevice);
}

static void render();
static void resizeScreen(const WORD width, const WORD height);

int App::Run()
{
	MSG msg;
	while (true)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				break;
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			updateMyData();
			render();
		}
	}

	return (int)msg.message;
}

static bool sbKeyPressed[UINT8_MAX];

LRESULT WndProc(const HWND hWnd, const UINT message, const WPARAM wParam, const LPARAM lParam)
{
	switch (message)
	{
	case WM_SIZE:
		if (spSwapChain != nullptr)
		{
			resizeScreen(LOWORD(lParam), HIWORD(lParam));
		}
		break;

	case WM_KEYDOWN:
		sbKeyPressed[wParam] = true;
		break;

	case WM_KEYUP:
		sbKeyPressed[wParam] = false;
		break;

	case WM_DESTROY:
		App::Destroy();
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	return 0;
}

static void render()
{
	const UINT stride = sizeof(BgVertex);
	const UINT offset = 0;

	spContext->IASetVertexBuffers(
		0,
		1,
		&spBgVertexBuffer,
		&stride,
		&offset
	);

	spContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	spContext->VSSetShader(spVS, nullptr, 0);

	spContext->RSSetViewports(1, &sViewport);

	spContext->PSSetShader(spPS, nullptr, 0);
	spContext->PSSetShaderResources(0, 1, &spBgTextureView);
	spContext->PSSetSamplers(0, 1, &spSampler);
	spContext->PSSetConstantBuffers(0, 1, &spPlayer1PosBufferGPU);
	spContext->PSSetConstantBuffers(1, 1, &spPlayer2PosBufferGPU);

	spContext->UpdateSubresource(spPlayer1PosBufferGPU, 0, nullptr, &sPlayer1PosBufferCPU, 0, 0);
	spContext->UpdateSubresource(spPlayer2PosBufferGPU, 0, nullptr, &sPlayer2PosBufferCPU, 0, 0);

	spContext->Draw(BG_VERTEX_COUNT, 0);

	spSwapChain->Present(1, 0);
}

static void resizeScreen(const WORD width, const WORD height)
{
	using namespace App;

	spContext->OMSetRenderTargets(0, 0, nullptr);
	ReleaseCOM(spRTV);

	HRESULT hr = spSwapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
	ASSERT(SUCCEEDED(hr), "ResizeBuffers failed");

	ID3D11Texture2D* pBackBuffer = nullptr;

	hr = spSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	ASSERT(SUCCEEDED(hr), "GetBuffer failed");

	assert(pBackBuffer != nullptr);
	hr = spDevice->CreateRenderTargetView(pBackBuffer, nullptr, &spRTV);
	ASSERT(SUCCEEDED(hr), "CreateRenderTargetView failed");
	ReleaseCOM(pBackBuffer);

	sViewport.Width = width;
	sViewport.Height = height;

	spContext->RSSetViewports(1, &sViewport);
	spContext->OMSetRenderTargets(1, &spRTV, nullptr);
}

constexpr float DELTA_DIST = 0.005f;

static void updateMyData()
{
#if SERVER
	if (sbKeyPressed[VK_UP])
	{
		sPlayer1PosBufferCPU.pos.y -= DELTA_DIST;
	}

	if (sbKeyPressed[VK_DOWN])
	{
		sPlayer1PosBufferCPU.pos.y += DELTA_DIST;
	}

	if (sbKeyPressed[VK_LEFT])
	{
		sPlayer1PosBufferCPU.pos.x -= DELTA_DIST;
	}

	if (sbKeyPressed[VK_RIGHT])
	{
		sPlayer1PosBufferCPU.pos.x += DELTA_DIST;
	}

	send(sSock, (char*)(&sPlayer1PosBufferCPU.pos), sizeof(ConstantBuffer::pos), 0);
#else
	if (sbKeyPressed[VK_UP])
	{
		sPlayer2PosBufferCPU.pos.y += DELTA_DIST;
	}

	if (sbKeyPressed[VK_DOWN])
	{
		sPlayer2PosBufferCPU.pos.y -= DELTA_DIST;
	}

	if (sbKeyPressed[VK_LEFT])
	{
		sPlayer2PosBufferCPU.pos.x -= DELTA_DIST;
	}

	if (sbKeyPressed[VK_RIGHT])
	{
		sPlayer2PosBufferCPU.pos.x += DELTA_DIST;
	}

	send(sSock, (char*)(&sPlayer2PosBufferCPU.pos), sizeof(ConstantBuffer::pos), 0);
#endif
}

static DWORD WINAPI updatePeerData(const LPVOID lpParam)
{
	while (true)
	{
#if SERVER
		recv(sSock, (char*)(&sPlayer2PosBufferCPU.pos), sizeof(ConstantBuffer::pos), 0);
#else
		recv(sSock, (char*)(&sPlayer1PosBufferCPU.pos), sizeof(ConstantBuffer::pos), 0);
#endif
	}

	return 0;
}
