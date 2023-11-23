#include "UETest.h"

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

// 创建并更新常量缓冲区
ID3D11Buffer* constantBuffer = nullptr;

ID3D11Texture2D* g_pTexture = NULL;
ID3D11ShaderResourceView* g_pTextureSRV = NULL;
ID3D11SamplerState* g_pSamplerState = nullptr; // 采样器状态

ID3D11Texture2D* depthBuffer = nullptr; 
ID3D11DepthStencilView* depthStencilView = nullptr;

bool GRHISupportsAsyncTextureCreation = false;

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

int BackBufferWidth = 0;
int BackBufferHeight = 0;

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

void StartRenderThead()
{
	// Start render thread
	g_pRenderThread = new  std::thread(RenderThread, g_hwnd);
}


void WaitRenderThead()
{
	// Wait for render thread to complete
	g_pRenderThread->join();

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
}

struct Vertex {
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT2 texcoord;  // 纹理坐标

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

	// 编译顶点着色器源代码
	ID3DBlob* shaderBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;

	CHECK_RESULT(D3DCompile(
		vertexShaderCode,                     // 顶点着色器源代码
		strlen(vertexShaderCode),             // 源代码长度
		nullptr,                                // 文件名，可用于错误输出
		nullptr,                                // 宏定义
		nullptr,                                // D3DInclude接口，用于包含其他文件
		"main",                                 // 入口点函数名
		"vs_5_0",                               // 目标着色器模型
		0,                                      // 编译选项
		0,                                      // 优化选项
		&shaderBlob,                            // 输出的着色器字节码
		&errorBlob                              // 输出的错误信息
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
		pixelShaderCode,                     // 顶点着色器源代码
		strlen(pixelShaderCode),             // 源代码长度
		nullptr,                                // 文件名，可用于错误输出
		nullptr,                                // 宏定义
		nullptr,                                // D3DInclude接口，用于包含其他文件
		"PSMain",                                 // 入口点函数名
		"ps_5_0",                               // 目标着色器模型
		0,                                      // 编译选项
		0,                                      // 优化选项
		&shaderBlob,                            // 输出的着色器字节码
		&errorBlob                              // 输出的错误信息
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

// 定义常量缓冲区结构体
struct ConstantBuffer
{
	XMMATRIX  viewProjectionMatrix;
};

void CreateConstBuffer()
{
	// 在初始化时创建常量缓冲区
	D3D11_BUFFER_DESC constantBufferDesc = {};
	constantBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	constantBufferDesc.ByteWidth = sizeof(ConstantBuffer);
	constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	constantBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	g_pd3dDevice->CreateBuffer(&constantBufferDesc, nullptr, &constantBuffer);

	// 设置观察矩阵
	XMVECTOR eyePosition = XMVectorSet(-1.0f, -1.0f, -1.0f, 1.0f);
	XMVECTOR focusPosition = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
	XMVECTOR upDirection = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMMATRIX viewMatrix = XMMatrixLookAtLH(eyePosition, focusPosition, upDirection);

	// 设置投影矩阵
	float fovAngleY = XM_PIDIV4; // 45度的视角
	float aspectRatio = 1280.0f / 720.0f; // 屏幕宽高比
	float nearZ = 0.1f; // 近裁剪面
	float farZ = 1000.0f; // 远裁剪面
	XMMATRIX projectionMatrix = XMMatrixPerspectiveFovLH(fovAngleY, aspectRatio, nearZ, farZ);

	// 计算视图投影矩阵
	XMMATRIX viewProjectionMatrix = viewMatrix * projectionMatrix;

	// 更新常量缓冲区
	ConstantBuffer cb;
	cb.viewProjectionMatrix = XMMatrixTranspose(viewProjectionMatrix);

	// 锁定常量缓冲区
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	g_pImmediateContext->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

	// 复制数据到常量缓冲区
	memcpy(mappedResource.pData, &cb, sizeof(ConstantBuffer));

	// 解锁常量缓冲区
	g_pImmediateContext->Unmap(constantBuffer, 0);
}

void CreateResource()
{
	// 定义纹理描述
	D3D11_TEXTURE2D_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(texDesc));
	texDesc.Width = 512;  // 纹理宽度
	texDesc.Height = 512;  // 纹理高度
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // 像素格式
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.MiscFlags = 0;


	std::vector<uint8_t> imageData(texDesc.Width * texDesc.Height * 4);

	uint8_t* Data = imageData.data();

	int blockSize = 64;  // 每块的大小
	int numBlocksX = texDesc.Height / blockSize;
	int numBlocksY = texDesc.Width / blockSize;

	for (int y = 0; y < numBlocksY; y++)
	{
		for (int x = 0; x < numBlocksX; x++)
		{

			int yHeight = y * numBlocksY * 64 * 64 * 4;
			int xWidth = x * 64 * 4;

			int blockStartIndex = yHeight + xWidth;

			// 生成随机的颜色值，范围为0到255
			uint8_t R = 255;
			uint8_t G = 255;
			uint8_t B = 255;
			uint8_t A = 255; // 设置透明度为不透明

			// 确定当前块的颜色
			if ((x + y) % 2 != 0)
			{
				// 生成随机的颜色值，范围为0到255
				R = 0;
				G = 0;
				B = 0;
				A = 255; // 设置透明度为不透明
			}

			for (int blockY = 0; blockY < 64; blockY++)
			{
				for (int blockX = 0; blockX < 64; blockX++)
				{
					int Index = blockStartIndex + blockY * texDesc.Width * 4 + blockX * 4;

					// 将颜色值写入数据中，注意颜色顺序是BGRA
					((uint8_t*)Data)[Index + 0] = B;
					((uint8_t*)Data)[Index + 1] = G;
					((uint8_t*)Data)[Index + 2] = R;
					((uint8_t*)Data)[Index + 3] = A;
				}
			}
		}
	}


	// 创建纹理
	D3D11_SUBRESOURCE_DATA initData;
	ZeroMemory(&initData, sizeof(initData));
	initData.pSysMem = imageData.data();  // 设置纹理数据，可以是一个有效的像素数据指针
	initData.SysMemPitch = texDesc.Width * 4/* 纹理数据每行占用的字节数 */;  // 设置纹理数据每行的字节数
	CHECK_RESULT(g_pd3dDevice->CreateTexture2D(&texDesc, &initData, &g_pTexture));

	// 创建纹理的着色器资源视图
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Format = texDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	g_pd3dDevice->CreateShaderResourceView(g_pTexture, &srvDesc, &g_pTextureSRV);


	D3D11_SAMPLER_DESC samplerDesc;
	ZeroMemory(&samplerDesc, sizeof(samplerDesc));
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; // 采样过滤器
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP; // U 轴（横轴）纹理寻址模式
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP; // V 轴（纵轴）纹理寻址模式
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; // W 轴（深度轴）纹理寻址模式
	samplerDesc.MipLODBias = 0.0f; // Mip 级别偏移
	samplerDesc.MaxAnisotropy = 1; // 各向异性过滤的最大采样次数
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS; // 深度比较函数
	samplerDesc.BorderColor[0] = 0.0f; // 边界颜色
	samplerDesc.BorderColor[1] = 0.0f;
	samplerDesc.BorderColor[2] = 0.0f;
	samplerDesc.BorderColor[3] = 0.0f;
	samplerDesc.MinLOD = 0; // 最小 Mip 级别
	samplerDesc.MaxLOD = 0; // 最大 Mip 级别

	CHECK_RESULT(g_pd3dDevice->CreateSamplerState(&samplerDesc, &g_pSamplerState));


	// 描述深度缓冲区
	DXGI_SAMPLE_DESC sampleDesc;
	sampleDesc.Count = 1;  // 采样数
	sampleDesc.Quality = 0;  // 质量级别
	DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;  // 深度缓冲区格式

	// 创建深度缓冲区纹理
	D3D11_TEXTURE2D_DESC textureDesc;
	ZeroMemory(&textureDesc, sizeof(textureDesc));
	textureDesc.Width = BackBufferWidth;  // 缓冲区宽度
	textureDesc.Height = BackBufferHeight;  // 缓冲区高度
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = depthBufferFormat;
	textureDesc.SampleDesc = sampleDesc;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	CHECK_RESULT(g_pd3dDevice->CreateTexture2D(&textureDesc, nullptr, &depthBuffer));


	// 创建深度模板视图
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
	ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));
	depthStencilViewDesc.Format = depthBufferFormat;
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDesc.Texture2D.MipSlice = 0;
	CHECK_RESULT(g_pd3dDevice->CreateDepthStencilView(depthBuffer, &depthStencilViewDesc, &depthStencilView));
}

void AsyncCreateTexture()
{

}

// Render thread function
void RenderThread(HWND hwnd)
{
	InitDevice();

	// Set the viewport
	D3D11_VIEWPORT vp;
	vp.Width = BackBufferWidth;
	vp.Height = BackBufferHeight;
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

		// 将常量缓冲区绑定到顶点着色器阶段的指定寄存器槽
		g_pImmediateContext->VSSetConstantBuffers(0, 1, &constantBuffer);

		// 设置纹理资源
		g_pImmediateContext->PSSetShaderResources(0, 1, &g_pTextureSRV);

		// 设置采样器状态
		g_pImmediateContext->PSSetSamplers(0, 1, &g_pSamplerState);

		g_pImmediateContext->DrawIndexed(36, 0, 0);

		// Present the back buffer to the screen
		g_pSwapChain->Present(1, 0);
	}
}