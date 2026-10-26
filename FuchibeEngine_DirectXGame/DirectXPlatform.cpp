#include "DirectXPlatform.h"
#include <cassert>
#include "Logger.h"
#include "StringUtility.h"
#include <format>
#include <filesystem>
#include <dxcapi.h>
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_win32.h"
#include "externals/imgui/imgui_impl_dx12.h"
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using namespace Microsoft::WRL;

// 初期化関数
void DirectXPlatform::Initialize(WindowsApi* windowsApi) {

	// null検出
	assert(windowsApi);

	// メンバ変数に記録する
	this->windowsApi = windowsApi;

	// デバイスの生成関数
	CreateDevice();

	// コマンド関連の初期化関数
	CommandInitialize();

	// スワップチェーンの生成関数
	CreateSwapChain();

	// デスクリプタヒープおよび深度バッファ関連の生成関数
	CreateDepthStencilTexture();

	// レンダーターゲットビューの初期化関数
	RenderTargetViewInitialize();

	// 深度ステンシルビューの初期化関数
	DepthStencilViewInitialize();

	// フェンスの初期化関数
	FenceInitialize();

	// ビューポート矩形の初期化関数
	ViewportRectInitialize();

	// シザリング矩形の初期化関数
	ScissorRectInitialize();

	// DXCコンパイラの生成関数
	CreateDxcCompiler();

	// ImGuiの初期化関数
	ImGuiInitialize();
}

// デバイスの生成関数
void DirectXPlatform::CreateDevice() {

	//==================
	// デバッグレイヤー
	//==================

#pragma region デバッグレイヤー

	HRESULT hr;

#ifdef _DEBUG

	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;

	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {

		// デバッグレイヤーを有効にする
		debugController->EnableDebugLayer();

		// さらにGPU側でもチェックを行うようにする
		debugController->SetEnableGPUBasedValidation(true);
	}

#endif

#pragma endregion

	//=================================
	// 使用するアダプタを決定する
	//=================================

#pragma region 使用するアダプタを決定する

	// DXGIファクトリーの生成
	dxgiFactory = nullptr;

	// HRESULTはWindows系のエラーコードであり、関数が成功したかどうかをSUCCEEDEDマクロで判定できる
	hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));

	// 初期化の根本的な部分でエラーが発生した場合はプログラムが間違っているか、どうにもできない場合が多いので、assertで止める
	assert(SUCCEEDED(hr));

	// 使用するアダプタ用の変数。最初に nullptr で初期化しておく
	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter = nullptr;

	// 良い順にアダプタを頼む
	for (UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i,
		DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND; i++) {

		// アダプタの情報を取得する
		DXGI_ADAPTER_DESC3 adapterDesc{};

		hr = useAdapter->GetDesc3(&adapterDesc);

		// 取得できないのは一大事なので、assertで止める
		assert(SUCCEEDED(hr));

		// ソフトウェアアダプタでなければ採用する
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {

			// 採用したアダプタの情報をログに出力する(wstringの方なので注意する)
			Logger::Log(StringUtility::ConvertString(std::format(L"Use Adapter:{}\n", adapterDesc.Description)));

			break;
		}
		// ソフトウェアアダプタの場合は使わないので、解放して nullptr にしておく
		useAdapter = nullptr;
	}

	// 適切なアダプタが見つからなかった場合は起動できないため、assertで止める
	assert(useAdapter != nullptr);

#pragma endregion

	//======================
	// D3D12Deviceの生成
	//======================

#pragma region D3D12Deviceの生成

	device = nullptr;

	// 機能レベルとログ出力用の文字列
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0
	};

	const char* featureLevelStrings[] = {
		"12.2", "12.1", "12.0"
	};

	// 高い順に生成できるか試していく
	for (size_t i = 0; i < _countof(featureLevels); i++) {

		// 採用したアダプタでデバイスを生成
		hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device));

		// 指定した機能レベルでデバイスが生成できたかを確認する
		if (SUCCEEDED(hr)) {
			Logger::Log(std::format("Feature Level:{}\n", featureLevelStrings[i]));
			break;
		}
	}

	// デバイスが生成できなかった場合は起動できないため、assertで止める
	assert(device.Get() != nullptr);

	// 初期化完了のログを出力する
	Logger::Log("Conplete create D3D12 Device!!!\n");

#pragma endregion

#pragma region エラー及び警告での停止

#ifdef _DEBUG

	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue = nullptr;

	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {

		// ヤバイエラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);

		// エラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

		// 抑制するメッセージのID
		D3D12_MESSAGE_ID denyIds[] = {

			// Windows11でのDXGIデバッグレイヤーとDX12デバッグレイヤーの相互作用バグによるエラーメッセージ
			// https://stackoverflow.com/questions/69805245/directx12-application-is-throws-crashing-in-windows-11
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};

		// 抑制するレベル
		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;

		// 指定したメッセージの表示を抑制する
		infoQueue->PushStorageFilter(&filter);
	}

#endif

	// ログ出力用のディレクトリを作成する
	std::filesystem::create_directory("logs");

#pragma endregion

}

// コマンド関連の初期化関数
void DirectXPlatform::CommandInitialize() {

	//=======================
	// コマンドリストの生成
	//=======================

#pragma region コマンドリストの生成

	commandAllocator = nullptr;
	HRESULT hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

	// コマンドアロケータの生成が上手くいかなかったので、起動できない
	assert(SUCCEEDED(hr));

	commandList = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));

	// コマンドリストの生成が上手くいかなかったので、起動できない
	assert(SUCCEEDED(hr));

#pragma endregion

	//=======================
	// コマンドキューの生成
	//=======================

#pragma region コマンドキューの生成

	commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));

	// コマンドキューの生成が上手くいかなかったので、起動できない
	assert(SUCCEEDED(hr));

#pragma endregion

}

// スワップチェーンの生成関数
void DirectXPlatform::CreateSwapChain() {

	//======================
	// スワップチェーンの生成
	//======================

#pragma region スワップチェーンの生成

	swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};

	// 画面の幅。ウィンドウのクライアント領域を同じサイズにしておく
	swapChainDesc.Width = WindowsApi::kClientWidth;

	// 画面の高さ。ウィンドウのクライアント領域を同じサイズにしておく
	swapChainDesc.Height = WindowsApi::kClientHeight;

	// 色の形式
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	// マルチサンプルしない
	swapChainDesc.SampleDesc.Count = 1;

	// 描画のターゲットとして利用する
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

	// ダブルバッファ
	swapChainDesc.BufferCount = kBufferCount;

	// モニタに映したら、中身を破棄する
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	// コマンドキュー、ウィンドウハンドル、スワップチェーンの設定を渡して生成する
	HRESULT hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(), windowsApi->GetHwnd(), &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf()));
	assert(SUCCEEDED(hr));

#pragma endregion

}

// 深度バッファの生成関数
Microsoft::WRL::ComPtr<ID3D12Resource> DirectXPlatform::CreateDepthBuffer(const Microsoft::WRL::ComPtr<ID3D12Device>& device, int32_t width, int32_t height) {

	// 生成するResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};

	// Textureの幅
	resourceDesc.Width = width;

	// Textureの高さ
	resourceDesc.Height = height;

	// mipMapの数
	resourceDesc.MipLevels = 1;

	// 奥行き or 配列Textureの配列数
	resourceDesc.DepthOrArraySize = 1;

	// DepthStencilとして利用可能なフォーマット
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// サンプリングカウント(1固定)
	resourceDesc.SampleDesc.Count = 1;

	// 2次元
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

	// DepthStencilとして使う通知
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	// 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};

	// VRAM上に作る
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// 深度値のClear設定
	D3D12_CLEAR_VALUE depthClearValue{};

	// 1.0f(最大値)でクリア
	depthClearValue.DepthStencil.Depth = 1.0f;

	// フォーマット(Resourceと合わせる)
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// Resourceの生成
	depthStencilResource = nullptr;

	HRESULT hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue, IID_PPV_ARGS(&depthStencilResource));
	assert(SUCCEEDED(hr));

	return depthStencilResource;

}

// 各種デスクリプタヒープの生成関数
void DirectXPlatform::CreateDepthStencilTexture() {

	//================================
	// 全てのディスクリプタサイズの定義
	//================================

#pragma region 全てのディスクリプタサイズの定義

	descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

#pragma endregion

	//==========================
	// RTVDescriptorHeapの生成
	//==========================

#pragma region RTVDescriptorHeapの生成

	// RTV用のヒープでディスクリプタヒープの数は2。RTVはShader内で触るものではないので、shaderVisibleはfalse
	rtvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);

	// スワップチェーンからバッファを一時的に取得
	Microsoft::WRL::ComPtr<ID3D12Resource> tempBuffer0 = nullptr;
	HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&tempBuffer0));
	assert(SUCCEEDED(hr));

	Microsoft::WRL::ComPtr<ID3D12Resource> tempBuffer1 = nullptr;
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&tempBuffer1));
	assert(SUCCEEDED(hr));

	tempBuffer0->AddRef();
	tempBuffer1->AddRef();

	Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources[2] = {
		Microsoft::WRL::ComPtr<ID3D12Resource>(tempBuffer0.Get()),
		Microsoft::WRL::ComPtr<ID3D12Resource>(tempBuffer1.Get())
	};

#pragma endregion

	// SRV用のヒープでディスクリプタヒープの数は128。SRVはShader内で触るものなので、shaderVisibleはtrue
	srvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	//==========================
	// DepthStencilViewの作成
	//==========================

#pragma region DepthStencilViewの作成

	// DSV用のHeapでディスクリプタの数は1。DSVはShaderないで触るものではないので、ShaderVisibleはfalseにする
	dsvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

	// DSV(深度ビュー)の設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};

	// Format(基本的にResourceと合わせる)
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// 2Dテクスチャとして扱う
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	// DSVHeapの先頭ハンドルを取得
	D3D12_CPU_DESCRIPTOR_HANDLE dsvStartHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandleTarget = dsvStartHandle;
	dsvHandleTarget.ptr += (0 * descriptorSizeDSV);

	// 計算したハンドル位置にDSVを作成する
	device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, dsvHandleTarget);

#pragma endregion

}

// デスクリプタヒープ生成関数
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DirectXPlatform::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible) {

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};

	descriptorHeapDesc.Type = heapType;
	descriptorHeapDesc.NumDescriptors = numDescriptors;
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	// ディスクリプタヒープの生成
	HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));

	// ディスクリプタヒープ	を生成できなかったので、起動できない
	assert(SUCCEEDED(hr));

	return descriptorHeap;

}

// SRVの指定番号のCPUデスクリプタハンドルを取得する関数
D3D12_CPU_DESCRIPTOR_HANDLE DirectXPlatform::GetSRVCPUDescriptorHandle(uint32_t index) {
	return GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, index);
}

// SRVの指定番号のGPUデスクリプタハンドルを取得する関数
D3D12_GPU_DESCRIPTOR_HANDLE DirectXPlatform::GetSRVGPUDescriptorHandle(uint32_t index) {
	return GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, index);
}

// レンダーターゲットビューの初期化関数
void DirectXPlatform::RenderTargetViewInitialize() {

	//==================
	// RTVの作成
	//==================

#pragma region RTVの作成

	// RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};

	// 出力結果をSRGBに変換して書き込む
	rtvDesc.Format = kRtvFormat;

	// 2Dテクスチャとして書き込む
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	// ディスクリプタの先頭を取得する
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

	// RTVを2つ作るので、ディスクリプタを2つ用意する
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2]{};

	// まず1つ目のRTVを作る。1つ目は最初のところに作る。作る場所をこちらで指定する必要がある
	/*rtvHandles[0] = rtvStartHandle;
	device->CreateRenderTargetView(swapChainResources[0].Get(), &rtvDesc, rtvHandles[0]);*/

	// 2つ目のディスクリプタハンドルを得る
	/*rtvHandles[1].ptr = rtvHandles[0].ptr + descriptorSizeRTV;*/

	// 2つ目のRTVを作る
	/*device->CreateRenderTargetView(swapChainResources[1].Get(), &rtvDesc, rtvHandles[1]);*/

	// 裏表の2つ分
	for (uint32_t i = 0; i < 2; ++i) {
		// 2つ目のRTVを作る
		device->CreateRenderTargetView(swapChainResources[i].Get(), &rtvDesc, rtvHandles[i]);
	}

#pragma endregion

}

// 深度ステンシルビューの初期化関数
void DirectXPlatform::DepthStencilViewInitialize() {

#pragma region DepthStencilViewの生成

	// DSV用のHeapでディスクリプタの数は1。DSVはShaderないで触るものではないので、ShaderVisibleはfalseにする
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

	// DSV(深度ビュー)の設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};

	// Format(基本的にResourceと合わせる)
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// 2Dテクスチャとして扱う
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	// DSVHeapの先頭ハンドルを取得
	D3D12_CPU_DESCRIPTOR_HANDLE dsvStartHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandleTarget = dsvStartHandle;
	dsvHandleTarget.ptr += (0 * descriptorSizeDSV);

	// 計算したハンドル位置にDSVを作成する
	device->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, dsvHandleTarget);

#pragma endregion

}

// フェンスの初期化関数
void DirectXPlatform::FenceInitialize() {

	//======================
	// FenceとEventの生成
	//======================

#pragma region FenceとEventの生成

	// 初期値0のFenceを生成する
	fence = nullptr;
	uint64_t fenceValue = 0;
	HRESULT hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	// Fenceのシグナルを待つためのEventを生成する
	HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(fenceEvent != nullptr);

#pragma endregion

}

// ビューポート矩形の初期化関数
void DirectXPlatform::ViewportRectInitialize() {

	// Viewportの設定
	D3D12_VIEWPORT viewport{};

	// クライアント領域のサイズと同じにして、画面全体に表示する
	viewport.Width = WindowsApi::kClientWidth;
	viewport.Height = WindowsApi::kClientHeight;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

}

// シザリング矩形の初期化関数
void DirectXPlatform::ScissorRectInitialize() {

	// シザー矩形の設定
	D3D12_RECT scissorRect{};

	// 基本的にビューポートと同じ矩形が構成されるようにする
	scissorRect.left = 0;
	scissorRect.right = WindowsApi::kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = WindowsApi::kClientHeight;

}

// DXCコンパイラの生成関数
void DirectXPlatform::CreateDxcCompiler() {

	//==============
	// DXCの初期化
	//==============

#pragma region DXCの初期化

	// dxcompilerを初期化
	IDxcUtils* dxcUtils = nullptr;
	IDxcCompiler3* dxcCompiler = nullptr;

	HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));

	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	// 現時点でincludeはしないが、includeに対応するための設定をしておく
	IDxcIncludeHandler* dxcIncludeHandler = nullptr;
	hr = dxcUtils->CreateDefaultIncludeHandler(&dxcIncludeHandler);
	assert(SUCCEEDED(hr));

#pragma endregion

}

// ImGuiの初期化関数
void DirectXPlatform::ImGuiInitialize() {

	//================
	// ImGuiの初期化
	//================

#pragma region ImGuiの初期化

#ifdef USE_IMGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(windowsApi->GetHwnd());
	ImGui_ImplDX12_Init(
		device.Get(),
		kBufferCount,
		kRtvFormat,
		srvDescriptorHeap.Get(),
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart()
	);

	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();
#endif

}

// DescriptorCPUHandle取得関数
D3D12_CPU_DESCRIPTOR_HANDLE DirectXPlatform::GetCPUDescriptorHandle(const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index) {

	// ディスクリプタヒープの先頭のハンドルを取得する
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();

	// index分だけハンドルを進める
	handleCPU.ptr += (descriptorSize * index);

	return handleCPU;

}

// DescriptorGPUHandle取得関数
D3D12_GPU_DESCRIPTOR_HANDLE DirectXPlatform::GetGPUDescriptorHandle(const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index) {

	// ディスクリプタヒープの先頭のハンドルを取得する
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();

	// index分だけハンドルを進める
	handleGPU.ptr += (descriptorSize * index);

	return handleGPU;
}

// SRVGPUDescriptorHandle取得関数
//D3D12_GPU_DESCRIPTOR_HANDLE DirectXPlatform::GetSRVGPUDescriptorHandle(uint32_t index) {
//	return GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, index);
//}