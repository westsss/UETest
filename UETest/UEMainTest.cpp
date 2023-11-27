#include "UETest.h"
#include "LockFreePointerFIFOBase.h"
#include <cmath>
#include <iostream>
#include <string>

HWND g_hwnd;
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pImmediateContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pRenderTargetView = nullptr;

bool g_bQuit = false;

ID3D11Buffer* vertexBuffer = nullptr;
ID3D11Buffer* indexBuffer = nullptr;
ID3D11VertexShader* vertexShader = nullptr;
ID3D11PixelShader* pixelShader = nullptr;
ID3D11InputLayout* inputLayout = nullptr;
ID3D11RasterizerState* rasterizerState = nullptr;

// ���������³���������
ID3D11Buffer* constantBuffer = nullptr;

ID3D11Texture2D* g_pTexture = NULL;
ID3D11ShaderResourceView* g_pTextureSRV = NULL;
ID3D11SamplerState* g_pSamplerState = nullptr; // ������״̬

ID3D11Texture2D* depthBuffer = nullptr; 
ID3D11DepthStencilView* depthStencilView = nullptr;

bool GRHISupportsAsyncTextureCreation = false;
std::atomic<long> g_TextureCount;

bool g_bSyncCreateTexture = false;
HANDLE g_hConsole;

void RenderThread(HWND hwnd);


#define CHECK_RESULT(hr) \
if (FAILED(hr)) \
{ \
    LPWSTR pErrorMessage = nullptr; \
    DWORD result = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, \
        nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&pErrorMessage, 0, nullptr); \
    if (result != 0) \
    { \
        MessageBox(nullptr, pErrorMessage, L"Error", MB_OK | MB_ICONERROR); \
        LocalFree(pErrorMessage); \
    } \
    else \
    { \
        MessageBox(nullptr, L"Get Error Failed", L"Error", MB_OK | MB_ICONERROR); \
    } \
} 

#define SAFE_RELEASE(ptr) \
		if(ptr)	\
		{	\
			ptr->Release();	\
			ptr = nullptr;	\
		}

int BackBufferWidth = 0;
int BackBufferHeight = 0;

struct ColorData
{
	uint8_t rgba[4];
};

struct ColorDataFloat
{
	float rgba[4];

	ColorDataFloat(const ColorData& data)
	{
		for (int idx = 0; idx < 4; idx++)
		{
			rgba[idx] = data.rgba[idx];
		}
	}

	ColorDataFloat operator += (const ColorData& data)
	{
		for (int idx = 0; idx < 4; idx++)
		{
			rgba[idx] += data.rgba[idx];
		}

		return *this;
	}

	ColorDataFloat operator /= (float devide)
	{
		for (int idx = 0; idx < 4; idx++)
		{
			rgba[idx] /= devide;
		}

		return *this;
	}

	ColorData ToColorData()
	{
		ColorData data;

		for (int idx = 0; idx < 4; idx++)
		{
			data.rgba[idx] = static_cast<int>(rgba[idx]);
		}

		return data;
	}
};

// Entry point
void  InitDevice()
{

	RECT rect;
	GetClientRect(g_hwnd, &rect);

	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;

	BackBufferWidth = width;
	BackBufferHeight = height;

	// Initialize Direct3D
	DXGI_SWAP_CHAIN_DESC sd = {};
	sd.BufferCount = 1;
	sd.BufferDesc.Width = width;
	sd.BufferDesc.Height = height;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = g_hwnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;

	CHECK_RESULT(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
		D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, nullptr, &g_pImmediateContext));

	D3D11_FEATURE_DATA_THREADING ThreadingSupport = { 0 };
	g_pd3dDevice->CheckFeatureSupport(D3D11_FEATURE_THREADING, &ThreadingSupport, sizeof(ThreadingSupport));

	GRHISupportsAsyncTextureCreation = ThreadingSupport.DriverConcurrentCreates;

	// Create render target view
	ID3D11Texture2D* pBackBuffer = nullptr;
	g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
	g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
	pBackBuffer->Release();
}

std::thread* g_pRenderThread = nullptr;

int g_AsyncThreadNum = 1;
std::vector<std::thread*> g_ListAsyncThread;

int DestMipMap = 8;
int SrcMipMap = 7;

class Command
{
public:
	Command() : IntermediateTextureRHI(nullptr)
	{

	}
	~Command()
	{
		SAFE_RELEASE(IntermediateTextureRHI);

		std::string str = "Release IntermediateTextureRHI \n";

		DWORD bytesWritten;
		WriteConsoleA(g_hConsole, str.c_str(), static_cast<DWORD>(str.length()), &bytesWritten, nullptr);

		g_TextureCount--;
	}

	void Execute()
	{
		int NumSharedMips = min(DestMipMap, SrcMipMap);

		int SourceMipOffset = SrcMipMap - NumSharedMips;
		int DestMipOffset = DestMipMap - NumSharedMips;

		for (int MipIndex = 0; MipIndex < NumSharedMips; ++MipIndex)
		{
			// Use the GPU to copy between mip-maps.
			g_pImmediateContext->CopySubresourceRegion(
				IntermediateTextureRHI,
				D3D11CalcSubresource(MipIndex + DestMipOffset, 0, DestMipMap),
				0,
				0,
				0,
				g_pTexture,
				D3D11CalcSubresource(MipIndex + SourceMipOffset, 0, SrcMipMap),
				NULL
			);
		}
	}

	ID3D11Texture2D* IntermediateTextureRHI;
};

FLockFreePointerFIFOBase<Command> g_CommandList;

void CreateMap(std::vector<ColorData>& imageData, int Width, int Height)
{
	imageData.resize(Width * Height);
	uint8_t* Data = reinterpret_cast<uint8_t*>(imageData.data());

	int blockSize = 64;  // ÿ��Ĵ�С
	int numBlocksX = Width / blockSize;
	int numBlocksY = Height / blockSize;

	for (int y = 0; y < numBlocksY; y++)
	{
		for (int x = 0; x < numBlocksX; x++)
		{

			int yHeight = y * numBlocksY * 64 * 64 * 4;
			int xWidth = x * 64 * 4;

			int blockStartIndex = yHeight + xWidth;

			// �����������ɫֵ����ΧΪ0��255
			uint8_t R = 255;
			uint8_t G = 255;
			uint8_t B = 255;
			uint8_t A = 255; // ����͸����Ϊ��͸��

			// ȷ����ǰ�����ɫ
			if ((x + y) % 2 != 0)
			{
				// �����������ɫֵ����ΧΪ0��255
				R = 0;
				G = 0;
				B = 0;
				A = 255; // ����͸����Ϊ��͸��
			}

			for (int blockY = 0; blockY < 64; blockY++)
			{
				for (int blockX = 0; blockX < 64; blockX++)
				{
					int Index = blockStartIndex + blockY * Width * 4 + blockX * 4;

					// ����ɫֵд�������У�ע����ɫ˳����BGRA
					((uint8_t*)Data)[Index + 0] = B;
					((uint8_t*)Data)[Index + 1] = G;
					((uint8_t*)Data)[Index + 2] = R;
					((uint8_t*)Data)[Index + 3] = A;
				}
			}
		}
	}
}

void GenerateMipmap(std::vector<ColorData>& imageData, std::vector<std::vector<ColorData>>& OutData, int Width, int Height)
{
	OutData.push_back(imageData);

	int mipWidth = Width >> 1;
	int mipHeight = Height >> 1;

	while (mipWidth > 0 && mipHeight > 0)
	{
		std::vector<ColorData> mipData;
		mipData.reserve(mipWidth * mipHeight);

		std::vector<ColorData>& input = OutData[OutData.size() - 1];

		for (int y = 0; y < Height; y += 2)
		{
			for (int x = 0; x < Width; x += 2)
			{
				ColorDataFloat sum = input[y * Width + x];
				sum += input[y * Width + (x + 1)];
				sum += input[(y + 1) * Width + x];
				sum += input[(y + 1) * Width + (x + 1)];

				sum /= 4;

				mipData.push_back(sum.ToColorData());
			}
		}

		OutData.push_back(mipData);

		Width = mipWidth;
		Height = mipHeight;

		mipWidth = Width >> 1;
		mipHeight = Height >> 1;
	}
}


void StreamIn()
{
	// ������������
	D3D11_TEXTURE2D_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(texDesc));
	texDesc.Width = 512;  // ������
	texDesc.Height = 512;  // ����߶�
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // ���ظ�ʽ
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	// ��������
	D3D11_SUBRESOURCE_DATA initData;
	ZeroMemory(&initData, sizeof(initData));
	initData.pSysMem = nullptr;  // �����������ݣ�������һ����Ч����������ָ��
	initData.SysMemPitch = 0;  // ������������ÿ�е��ֽ���
	CHECK_RESULT(g_pd3dDevice->CreateTexture2D(&texDesc, &initData, &g_pTexture));

	// �����������ɫ����Դ��ͼ
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	g_pd3dDevice->CreateShaderResourceView(g_pTexture, &srvDesc, &g_pTextureSRV);
}

void InitRHIStreamableTextureResource()
{
	// ������������
	D3D11_TEXTURE2D_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(texDesc));
	texDesc.Width = 1 << (DestMipMap - 1);  // ������
	texDesc.Height = 1 << (DestMipMap - 1);  // ����߶�
	texDesc.MipLevels = DestMipMap;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // ���ظ�ʽ
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	std::vector<ColorData> imageData;
	CreateMap(imageData, texDesc.Width, texDesc.Height);


	Command* pCommand = new Command();

	// ��������
	CHECK_RESULT(g_pd3dDevice->CreateTexture2D(&texDesc, nullptr, &pCommand->IntermediateTextureRHI));
	g_pImmediateContext->UpdateSubresource(pCommand->IntermediateTextureRHI, 0, nullptr, imageData.data(), texDesc.Width * sizeof(DWORD), 0);

	g_TextureCount++;
	// ������ݵ�����̨����
	
	std::string str = std::to_string(g_TextureCount);
	str += "\n";

	DWORD bytesWritten;
	WriteConsoleA(g_hConsole, str.c_str(), static_cast<DWORD>(str.length()), &bytesWritten, nullptr);

	g_CommandList.Enqueue(pCommand);
}

void AsyncCreateTexture()
{
	while (!g_bQuit)
	{
		if (!g_bSyncCreateTexture)
		{
			InitRHIStreamableTextureResource();
		}
	}
}

void StartRenderThead()
{
	// �����µĿ���̨����
	AllocConsole();
	g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

	InitDevice();

	// Start render thread
	g_pRenderThread = new  std::thread(RenderThread, g_hwnd);

	for (int idx = 0; idx < g_AsyncThreadNum; idx++)
	{
		g_ListAsyncThread.push_back(new std::thread(AsyncCreateTexture));
	}
}


void WaitRenderThead()
{
	// Wait for render thread to complete
	g_pRenderThread->join();

	// Wait for AsyncCreate thread to complete
	for (auto AsyncThead : g_ListAsyncThread)
	{
		AsyncThead->join();
	}

	// Cleanup
	g_pRenderTargetView->Release();
	g_pSwapChain->Release();
	g_pImmediateContext->Release();
	g_pd3dDevice->Release();

	vertexBuffer->Release();
	indexBuffer->Release();
	vertexShader->Release();
	pixelShader->Release();
	inputLayout->Release();
	rasterizerState->Release();

	g_pTexture->Release();
	g_pTextureSRV->Release();
	g_pSamplerState->Release();

	depthBuffer->Release();
	depthStencilView->Release();

	delete g_pRenderThread;
	g_pRenderThread = nullptr;

	// Wait for AsyncCreate thread to complete
	for (auto AsyncThead : g_ListAsyncThread)
	{
		delete AsyncThead;
	}

	g_ListAsyncThread.clear();

	// �رտ���̨����
	FreeConsole();
	g_hConsole = INVALID_HANDLE_VALUE;
}

struct Vertex {
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT2 texcoord;  // ��������

};

void CreateVertexBuffer() {
	Vertex vertices[] = {
		{ DirectX::XMFLOAT3(-0.5f, -0.5f, -0.5f), DirectX::XMFLOAT2(0.0f, 1.0f) },
		{ DirectX::XMFLOAT3(-0.5f,  0.5f, -0.5f), DirectX::XMFLOAT2(0.0f, 0.0f) },
		{ DirectX::XMFLOAT3(0.5f,  0.5f, -0.5f), DirectX::XMFLOAT2(1.0f, 0.0f) },
		{ DirectX::XMFLOAT3(0.5f, -0.5f, -0.5f), DirectX::XMFLOAT2(1.0f, 1.0f) },
		{ DirectX::XMFLOAT3(-0.5f, -0.5f,  0.5f), DirectX::XMFLOAT2(0.0f, 1.0f) },
		{ DirectX::XMFLOAT3(-0.5f,  0.5f,  0.5f), DirectX::XMFLOAT2(0.0f, 0.0f) },
		{ DirectX::XMFLOAT3(0.5f,  0.5f,  0.5f), DirectX::XMFLOAT2(1.0f, 0.0f) },
		{ DirectX::XMFLOAT3(0.5f, -0.5f,  0.5f), DirectX::XMFLOAT2(1.0f, 1.0f) }
	};

	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = sizeof(Vertex) * 8;
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bufferDesc.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = vertices;
	g_pd3dDevice->CreateBuffer(&bufferDesc, &initData, &vertexBuffer);
}

void CreateIndexBuffer() {
	unsigned int indices[] = {
		0, 1, 2, 2, 3, 0,
		4, 5, 6, 6, 7, 4,
		0, 4, 7, 7, 3, 0,
		1, 5, 6, 6, 2, 1,
		0, 1, 5, 5, 4, 0,
		3, 2, 6, 6, 7, 3
	};

	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = sizeof(unsigned int) * 36;
	bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bufferDesc.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA initData = {};
	initData.pSysMem = indices;
	g_pd3dDevice->CreateBuffer(&bufferDesc, &initData, &indexBuffer);
}

void CreateShaders() {
	const char* vertexShaderCode = R"(
		cbuffer ConstantBuffer : register(b0)
		{
			float4x4 viewProjectionMatrix;
		};

		struct VertexInput {
            float3 position : POSITION;
			float2 texcoord : TEXCOORD;
        };

		struct PixelInput {
            float4 position : SV_POSITION;
			float2 texcoord : TEXCOORD;
        };

        PixelInput main(VertexInput input) {
            PixelInput output;
            output.position = mul(float4(input.position, 1), viewProjectionMatrix);
			output.texcoord = input.texcoord;
            return output;
        }
    )";

	// ���붥����ɫ��Դ����
	ID3DBlob* shaderBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;

	CHECK_RESULT(D3DCompile(
		vertexShaderCode,                     // ������ɫ��Դ����
		strlen(vertexShaderCode),             // Դ���볤��
		nullptr,                                // �ļ����������ڴ������
		nullptr,                                // �궨��
		nullptr,                                // D3DInclude�ӿڣ����ڰ��������ļ�
		"main",                                 // ��ڵ㺯����
		"vs_5_0",                               // Ŀ����ɫ��ģ��
		0,                                      // ����ѡ��
		0,                                      // �Ż�ѡ��
		&shaderBlob,                            // �������ɫ���ֽ���
		&errorBlob                              // ����Ĵ�����Ϣ
	));


	D3D11_INPUT_ELEMENT_DESC inputElementDesc[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};

	UINT numElements = ARRAYSIZE(inputElementDesc);

	CHECK_RESULT(g_pd3dDevice->CreateVertexShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &vertexShader));
	g_pd3dDevice->CreateInputLayout(inputElementDesc, numElements, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), &inputLayout);


	const char* pixelShaderCode = R"(
			Texture2D shaderTexture : register(t0);
			SamplerState samplerState  : register(s0);

			struct PS_INPUT
			{
				float4 position : SV_POSITION;
				float2 texcoord : TEXCOORD;
			};

			float4 PSMain(PS_INPUT input) : SV_Target
			{
				return shaderTexture.Sample(samplerState, input.texcoord);
			}
			)";

	CHECK_RESULT(D3DCompile(
		pixelShaderCode,                     // ������ɫ��Դ����
		strlen(pixelShaderCode),             // Դ���볤��
		nullptr,                                // �ļ����������ڴ������
		nullptr,                                // �궨��
		nullptr,                                // D3DInclude�ӿڣ����ڰ��������ļ�
		"PSMain",                                 // ��ڵ㺯����
		"ps_5_0",                               // Ŀ����ɫ��ģ��
		0,                                      // ����ѡ��
		0,                                      // �Ż�ѡ��
		&shaderBlob,                            // �������ɫ���ֽ���
		&errorBlob                              // ����Ĵ�����Ϣ
	));

	CHECK_RESULT(g_pd3dDevice->CreatePixelShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &pixelShader));


}

void CreateRasterizerState() {
	D3D11_RASTERIZER_DESC rasterizerDesc = {};
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	rasterizerDesc.FrontCounterClockwise = FALSE;
	rasterizerDesc.DepthClipEnable = TRUE;

	g_pd3dDevice->CreateRasterizerState(&rasterizerDesc, &rasterizerState);
	g_pImmediateContext->RSSetState(rasterizerState);
}

// ���峣���������ṹ��
struct ConstantBuffer
{
	XMMATRIX  viewProjectionMatrix;
};

void CreateConstBuffer()
{
	// �ڳ�ʼ��ʱ��������������
	D3D11_BUFFER_DESC constantBufferDesc = {};
	constantBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	constantBufferDesc.ByteWidth = sizeof(ConstantBuffer);
	constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	constantBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	g_pd3dDevice->CreateBuffer(&constantBufferDesc, nullptr, &constantBuffer);

	// ���ù۲����
	XMVECTOR eyePosition = XMVectorSet(-1.0f, -1.0f, -1.0f, 1.0f);
	XMVECTOR focusPosition = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	XMVECTOR upDirection = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMMATRIX viewMatrix = XMMatrixLookAtLH(eyePosition, focusPosition, upDirection);

	// ����ͶӰ����
	float fovAngleY = XM_PIDIV4; // 45�ȵ��ӽ�
	float aspectRatio = 1280.0f / 720.0f; // ��Ļ��߱�
	float nearZ = 0.1f; // ���ü���
	float farZ = 1000.0f; // Զ�ü���
	XMMATRIX projectionMatrix = XMMatrixPerspectiveFovLH(fovAngleY, aspectRatio, nearZ, farZ);

	// ������ͼͶӰ����
	XMMATRIX viewProjectionMatrix = viewMatrix * projectionMatrix;

	// ���³���������
	ConstantBuffer cb;
	cb.viewProjectionMatrix = XMMatrixTranspose(viewProjectionMatrix);

	// ��������������
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	g_pImmediateContext->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

	// �������ݵ�����������
	memcpy(mappedResource.pData, &cb, sizeof(ConstantBuffer));

	// ��������������
	g_pImmediateContext->Unmap(constantBuffer, 0);
}


void CreateResource()
{
	// ������������
	D3D11_TEXTURE2D_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(texDesc));
	texDesc.Width = 1 << (SrcMipMap - 1);  // ������
	texDesc.Height = 1 << (SrcMipMap - 1);  // ����߶�
	texDesc.MipLevels = SrcMipMap;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // ���ظ�ʽ
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;

	std::vector<ColorData> imageData;
	CreateMap(imageData, texDesc.Width, texDesc.Height);

	
	std::vector<std::vector<ColorData>> mipMapData;
	GenerateMipmap(imageData, mipMapData, texDesc.Width, texDesc.Height);

	// ��������
	CHECK_RESULT(g_pd3dDevice->CreateTexture2D(&texDesc, nullptr, &g_pTexture));


	UINT numMipLevels = static_cast<UINT>(mipMapData.size());
	for (UINT mipLevel = 0; mipLevel < numMipLevels; ++mipLevel)
	{
		// ���㵱ǰMipmap����Ŀ�Ⱥ͸߶�
		UINT mipWidth = 1 << (numMipLevels - mipLevel - 1);
		UINT mipHeight = 1 << (numMipLevels - mipLevel - 1);

		// ���㵱ǰMipmap��������ݴ�С
		UINT mipDataSize = mipWidth * mipHeight * sizeof(DWORD);

		// ���µ�ǰMipmap���������
		g_pImmediateContext->UpdateSubresource(g_pTexture, mipLevel, nullptr, mipMapData[mipLevel].data(), mipWidth * sizeof(DWORD), 0);
	}


	// �����������ɫ����Դ��ͼ
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = numMipLevels;
	g_pd3dDevice->CreateShaderResourceView(g_pTexture, &srvDesc, &g_pTextureSRV);


	D3D11_SAMPLER_DESC samplerDesc;
	ZeroMemory(&samplerDesc, sizeof(samplerDesc));
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; // ����������
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP; // U �ᣨ���ᣩ����Ѱַģʽ
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP; // V �ᣨ���ᣩ����Ѱַģʽ
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; // W �ᣨ����ᣩ����Ѱַģʽ
	samplerDesc.MipLODBias = 0.0f; // Mip ����ƫ��
	samplerDesc.MaxAnisotropy = 1; // �������Թ��˵�����������
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS; // ��ȱȽϺ���
	samplerDesc.BorderColor[0] = 0.0f; // �߽���ɫ
	samplerDesc.BorderColor[1] = 0.0f;
	samplerDesc.BorderColor[2] = 0.0f;
	samplerDesc.BorderColor[3] = 0.0f;
	samplerDesc.MinLOD = 0; // ��С Mip ����
	samplerDesc.MaxLOD = static_cast<float>(numMipLevels - 1); // ��� Mip ����

	CHECK_RESULT(g_pd3dDevice->CreateSamplerState(&samplerDesc, &g_pSamplerState));


	// ������Ȼ�����
	DXGI_SAMPLE_DESC sampleDesc;
	sampleDesc.Count = 1;  // ������
	sampleDesc.Quality = 0;  // ��������
	DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;  // ��Ȼ�������ʽ

	// ������Ȼ���������
	D3D11_TEXTURE2D_DESC textureDesc;
	ZeroMemory(&textureDesc, sizeof(textureDesc));
	textureDesc.Width = BackBufferWidth;  // ���������
	textureDesc.Height = BackBufferHeight;  // �������߶�
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = depthBufferFormat;
	textureDesc.SampleDesc = sampleDesc;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	CHECK_RESULT(g_pd3dDevice->CreateTexture2D(&textureDesc, nullptr, &depthBuffer));


	// �������ģ����ͼ
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
	ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));
	depthStencilViewDesc.Format = depthBufferFormat;
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDesc.Texture2D.MipSlice = 0;
	CHECK_RESULT(g_pd3dDevice->CreateDepthStencilView(depthBuffer, &depthStencilViewDesc, &depthStencilView));
}

// Render thread function
void RenderThread(HWND hwnd)
{
	// Set the viewport
	D3D11_VIEWPORT vp;
	vp.Width =  static_cast<float>(BackBufferWidth);
	vp.Height = static_cast<float>(BackBufferHeight);
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = 0;   
	vp.TopLeftY = 0;
	g_pImmediateContext->RSSetViewports(1, &vp);

	CreateVertexBuffer();
	CreateIndexBuffer();
	CreateShaders();
	CreateRasterizerState();
	CreateConstBuffer();
	CreateResource();

	// Main rendering loop
	while (!g_bQuit)
	{
		if (g_bSyncCreateTexture)
		{
			InitRHIStreamableTextureResource();
		}

		Command* pCommand = g_CommandList.Dequeue();
		if (pCommand)
		{
			pCommand->Execute();
			
			delete pCommand;
			pCommand = nullptr;
		}

 		// Clear the back buffer
 		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
 		g_pImmediateContext->OMSetRenderTargets(1, &g_pRenderTargetView, depthStencilView);
 		g_pImmediateContext->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);
 		g_pImmediateContext->ClearRenderTargetView(g_pRenderTargetView, clearColor);
 
 		// Perform rendering operations
 		UINT stride = sizeof(Vertex);
 		UINT offset = 0;
 
 		g_pImmediateContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
 		g_pImmediateContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);
 		g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 		g_pImmediateContext->IASetInputLayout(inputLayout);
 
 		g_pImmediateContext->VSSetShader(vertexShader, nullptr, 0);
 		g_pImmediateContext->PSSetShader(pixelShader, nullptr, 0);
 
 		// �������������󶨵�������ɫ���׶ε�ָ���Ĵ�����
 		g_pImmediateContext->VSSetConstantBuffers(0, 1, &constantBuffer);
 
 		// ����������Դ
 		g_pImmediateContext->PSSetShaderResources(0, 1, &g_pTextureSRV);
 
 		// ���ò�����״̬
 		g_pImmediateContext->PSSetSamplers(0, 1, &g_pSamplerState);
 
 		g_pImmediateContext->DrawIndexed(36, 0, 0);
 
 		// Present the back buffer to the screen
 		g_pSwapChain->Present(1, 0);
	}
}