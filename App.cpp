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
	XMFLOAT3 player1Pos;
	float dummy1;

	XMFLOAT3 player2Pos;
	float dummy2;
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

static ConstantBuffer sConstantBufferCPU;
static ID3D11Buffer* spConstantBufferGPU = nullptr;

static ID3D11PixelShader* spPS = nullptr;

static constexpr int BG_VERTEX_COUNT = 4;
static ID3D11Buffer* spBgVertexBuffer = nullptr;

static ID3D11Texture2D* spBgTexture = nullptr;
static ID3D11ShaderResourceView* spBgTextureView = nullptr;
static ID3D11SamplerState* spSampler = nullptr;

static D3D11_VIEWPORT sViewport;

static ID3D11RenderTargetView* spRTV = nullptr;

// 소켓
static SOCKET sock;

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

		sConstantBufferCPU.player1Pos = { -0.05f, 0.f, 0.f };
		sConstantBufferCPU.player2Pos = { 0.05f, 0.f, 0.f };

		bufferDesc.Usage = D3D11_USAGE_DEFAULT;
		bufferDesc.ByteWidth = sizeof(ConstantBuffer);
		bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bufferDesc.StructureByteStride = sizeof(XMMATRIX);

		initData.pSysMem = &sConstantBufferCPU;

		hr = spDevice->CreateBuffer(&bufferDesc, &initData, &spConstantBufferGPU);
		ASSERT(SUCCEEDED(hr), "CreateBuffer for constantBuffer failed");

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
		WSAStartup(MAKEWORD(2, 2), &wsaData);

		int errorCode = WSAGetLastError();
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

		sock = accept(listeningSock, reinterpret_cast<sockaddr*>(&clientSockInfo), &clientSize);
		if (sock == INVALID_SOCKET)
		{
			ASSERT(false, "accept failed");

			closesocket(listeningSock);
			WSACleanup();

			return;
		}

		closesocket(listeningSock);
#elif CLIENT
		sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock == INVALID_SOCKET)
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

		errorCode = connect(sock, reinterpret_cast<sockaddr*>(&hint), sizeof(hint));
		if (errorCode == SOCKET_ERROR)
		{
			ASSERT(false, "connect fialed");

			closesocket(sock);
			WSACleanup();

			return;
		}
#endif
		DWORD recvTimeout = 17;
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&recvTimeout, sizeof(recvTimeout));
	}
}

void App::Destroy()
{
	closesocket(sock);
	WSACleanup();

	ReleaseCOM(spRTV);
	ReleaseCOM(spBgTextureView);
	ReleaseCOM(spBgTexture);
	ReleaseCOM(spBgVertexBuffer);
	ReleaseCOM(spConstantBufferGPU);
	ReleaseCOM(spVS);
	ReleaseCOM(spPS);
	ReleaseCOM(spSwapChain);
	ReleaseCOM(spContext);
	ReleaseCOM(spDevice);
}

static void render();
static void resizeScreen(const WORD width, const WORD height);
static void update();

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
			update();
			render();
		}
	}

	return (int)msg.message;
}

static bool sbKeyPressedServer[UINT8_MAX];
static bool sbKeyPressedClient[UINT8_MAX];

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
#if SERVER
		sbKeyPressedServer[wParam] = true;
		send(sock, (char*)sbKeyPressedServer, sizeof(sbKeyPressedServer), 0);
#elif CLIENT
		sbKeyPressedClient[wParam] = true;
		send(sock, (char*)sbKeyPressedClient, sizeof(sbKeyPressedClient), 0);
#endif
		break;

	case WM_KEYUP:
#if SERVER
		sbKeyPressedServer[wParam] = false;
		send(sock, (char*)sbKeyPressedServer, sizeof(sbKeyPressedServer), 0);
#elif CLIENT
		sbKeyPressedClient[wParam] = false;
		send(sock, (char*)sbKeyPressedClient, sizeof(sbKeyPressedClient), 0);
#endif
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
}

	return 0;
}

void render()
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
	spContext->PSSetConstantBuffers(0, 1, &spConstantBufferGPU);

	spContext->Draw(BG_VERTEX_COUNT, 0);

	spSwapChain->Present(1, 0);
}

void resizeScreen(const WORD width, const WORD height)
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

static float clamp(const float value, const float min, const float max);
static void updatePosition();

constexpr float deltaDist = 0.005f;

static void update()
{
#if SERVER
	recv(sock, (char*)sbKeyPressedClient, sizeof(sbKeyPressedClient), 0);
#elif CLIENT
	recv(sock, (char*)sbKeyPressedServer, sizeof(sbKeyPressedServer), 0);
#endif
	updatePosition();

	spContext->UpdateSubresource(spConstantBufferGPU, 0, nullptr, &sConstantBufferCPU, 0, 0);
}

static float clamp(const float value, const float min, const float max)
{
	if (value < min)
	{
		return min;
	}

	if (value > max)
	{
		return max;
	}

	return value;
}

static void updatePosition()
{
	if (sbKeyPressedServer[VK_UP])
	{
		sConstantBufferCPU.player1Pos.y -= deltaDist;
	}

	if (sbKeyPressedServer[VK_DOWN])
	{
		sConstantBufferCPU.player1Pos.y += deltaDist;
	}

	if (sbKeyPressedServer[VK_LEFT])
	{
		sConstantBufferCPU.player1Pos.x -= deltaDist;
	}

	if (sbKeyPressedServer[VK_RIGHT])
	{
		sConstantBufferCPU.player1Pos.x += deltaDist;
	}

	if (sbKeyPressedClient[VK_UP])
	{
		sConstantBufferCPU.player2Pos.y += deltaDist;
	}

	if (sbKeyPressedClient[VK_DOWN])
	{
		sConstantBufferCPU.player2Pos.y -= deltaDist;
	}

	if (sbKeyPressedClient[VK_LEFT])
	{
		sConstantBufferCPU.player2Pos.x -= deltaDist;
	}

	if (sbKeyPressedClient[VK_RIGHT])
	{
		sConstantBufferCPU.player2Pos.x += deltaDist;
	}

	sConstantBufferCPU.player1Pos.x = clamp(sConstantBufferCPU.player1Pos.x, -1.f, 1.f);
	sConstantBufferCPU.player1Pos.y = clamp(sConstantBufferCPU.player1Pos.y, -1.f, 1.f);
	sConstantBufferCPU.player1Pos.x = clamp(sConstantBufferCPU.player1Pos.x, -1.f, 1.f);
	sConstantBufferCPU.player1Pos.y = clamp(sConstantBufferCPU.player1Pos.y, -1.f, 1.f);

	sConstantBufferCPU.player2Pos.x = clamp(sConstantBufferCPU.player2Pos.x, -1.f, 1.f);
	sConstantBufferCPU.player2Pos.y = clamp(sConstantBufferCPU.player2Pos.y, -1.f, 1.f);
	sConstantBufferCPU.player2Pos.x = clamp(sConstantBufferCPU.player2Pos.x, -1.f, 1.f);
	sConstantBufferCPU.player2Pos.y = clamp(sConstantBufferCPU.player2Pos.y, -1.f, 1.f);
}