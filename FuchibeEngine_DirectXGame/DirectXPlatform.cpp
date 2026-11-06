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
#include "externals/DirectXTex/d3dx12.h"
#include <thread>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using namespace Microsoft::WRL;

// 初期化関数
void DirectXPlatform::Initialize(WindowsApi* pWindowsApi) {

	// null検出
	assert(pWindowsApi);

	// メンバ変数に記録する
	this->windowsApi = pWindowsApi;

	// FPS固定初期化
	InitializeFixFps();

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
Microsoft::WRL::ComPtr<ID3D12Resource> DirectXPlatform::CreateDepthBuffer(int32_t width, int32_t height) {

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

	// ディスクリプタサイズの取得
	descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	// RTV用ヒープの作成
	rtvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);

	// スワップチェーンからメンバ変数の swapChainResources へ直接取得する
	for (UINT i = 0; i < kBufferCount; ++i) {
		HRESULT hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&swapChainResources[i]));
		assert(SUCCEEDED(hr));
	}

	// SRV用ヒープの作成
	srvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	// 深度バッファリソースの生成（幅・高さを指定）
	depthStencilResource = CreateDepthBuffer(WindowsApi::kClientWidth, WindowsApi::kClientHeight);

	// DSV用ヒープの作成
	dsvDescriptorHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
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

// CompileShader関数
Microsoft::WRL::ComPtr<IDxcBlob> DirectXPlatform::CompileShader(const std::wstring& filePath, const wchar_t* profile) {

	// これからシェーダーをコンパイルする旨をログに出力する
	Logger::Log(StringUtility::ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));

	// hlslファイルを読む
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);

	// 読めなかったら止める
	assert(SUCCEEDED(hr));

	// 読み込んだファイルの内容を設定する
	DxcBuffer shaderSourceBuffer{};
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();

	// UTF8の文字コードであることを通知する
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;

	LPCWSTR arguments[] = {
		filePath.c_str(),
		L"-E", L"main",
		L"-T", profile,
		L"-Zi", L"-Qembed_debug",
		L"-Od",
		L"-Zpr",
	};

	// 実際にシェーダーをコンパイルする
	Microsoft::WRL::ComPtr<IDxcResult> shaderResult = nullptr;
	hr = dxcCompiler->Compile(&shaderSourceBuffer, arguments, _countof(arguments), dxcIncludeHandler.Get(), IID_PPV_ARGS(&shaderResult));

	// コンパイルエラーではなくdxcが起動できないなどの根本的なエラーが発生した場合は止める
	assert(SUCCEEDED(hr));

	// 警告・エラーが出ていたらログに出力して止める
	Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);

	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		Logger::Log(shaderError->GetStringPointer());

		// 警告・エラーが出ている場合は止める
		assert(false);
	}

	// コンパイル結果から実行用のバイナリ部分を取得する
	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));

	// 成功したログを出力する
	Logger::Log(StringUtility::ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));

	// 実行用のバイナリを返す
	return shaderBlob;

}

// リソース生成関数
Microsoft::WRL::ComPtr<ID3D12Resource> DirectXPlatform::CreateBufferResource(size_t sizeInBytes) {

	// 頂点リソース用のヒープの設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};

	// uploadヒープを使う
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	// 頂点リソースの設定
	D3D12_RESOURCE_DESC vertexResourceDesc{};

	// バッファリソース。テクスチャの場合はまた別の設定をする
	vertexResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;

	// リソースのサイズ。引数のサイズを適用する
	vertexResourceDesc.Width = sizeInBytes;

	vertexResourceDesc.Height = 1;
	vertexResourceDesc.DepthOrArraySize = 1;
	vertexResourceDesc.MipLevels = 1;
	vertexResourceDesc.SampleDesc.Count = 1;

	// バッファの場合はRowMajorにする必要がある
	vertexResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	// 実際に頂点リソースを生成する
	Microsoft::WRL::ComPtr<ID3D12Resource> bufferResource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&bufferResource));
	assert(SUCCEEDED(hr));

	return bufferResource;
}

// テクスチャリソースの生成関数
Microsoft::WRL::ComPtr<ID3D12Resource> DirectXPlatform::CreateTextureResource(const DirectX::TexMetadata& metaData) {

	// metaDataを基にResourceを設定する
	D3D12_RESOURCE_DESC resourceDesc{};

	// Textureの幅
	resourceDesc.Width = UINT(metaData.width);

	// Textureの高さ
	resourceDesc.Height = UINT(metaData.height);

	// mipMapの数
	resourceDesc.MipLevels = UINT16(metaData.mipLevels);

	// 奥行き or 配列Textureの配列数
	resourceDesc.DepthOrArraySize = UINT16(metaData.arraySize);

	// TextureのFormat
	resourceDesc.Format = metaData.format;

	// サンプリングカウント(1固定)
	resourceDesc.SampleDesc.Count = 1;

	// Textureの次元数。普段使っているのは2次元
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metaData.dimension);

	// 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties{};

	// 細かい設定を行う
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// WriteBackポリシーでCPUアクセス可能
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;

	// プロセッサの近くに配置
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// Resourceを生成して、Returnする
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));

	return resource;

}

// リソース転送関数
Microsoft::WRL::ComPtr<ID3D12Resource> DirectXPlatform::UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages) {

	// 中間リソース
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;

	// PrepareUploadを利用して、読み込んだデータからDirectX12用のSubresourceの配列を作成する
	DirectX::PrepareUpload(device.Get(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);

	// Subresourceの数を基に、コピー元となるintermediateResourceに必要なサイズを計算する
	uint64_t intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, UINT(subresources.size()));

	// 計算したサイズでintermediateResourceを作成する
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = CreateBufferResource(intermediateSize);

	// データ転送をコマンドに積む
	UpdateSubresources(commandList.Get(), texture.Get(), intermediateResource.Get(), 0, 0, UINT(subresources.size()), subresources.data());

	// intermediateResourceを返す
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;

	commandList->ResourceBarrier(1, &barrier);
	return intermediateResource;
}

// テクスチャファイル読み込み関数
DirectX::ScratchImage DirectXPlatform::LoadTexture(const std::string& filePath) {

	// テクスチャファイルを読んで、プログラムで扱えるようにする
	DirectX::ScratchImage image{};
	std::wstring filePathW = StringUtility::ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));

	// ミップマップの作成
	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));

	// ミップマップ付きのデータを返す
	return mipImages;
}

// 描画前処理関数
void DirectXPlatform::PreDraw() {

	// これから書き込むバックバッファのインデックスを取得する
	UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

	//==========================
	// TransitionBarrierを張る
	//==========================

#pragma region TransitionBarrierを張る

	// TransitionBarrierの設定
	D3D12_RESOURCE_BARRIER barrier{};

	// 今回のバリアはTransition
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

	// Noneにしておく
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

	// バリアを張る対象のResourceを指定する。現在のバックバッファに対して行う
	barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();

	// 遷移前(現在)のResourceState
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;

	// 遷移後のResourceState
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

	// TransitionBarrierを張る
	commandList->ResourceBarrier(1, &barrier);

#pragma endregion

	// 描画先のRTVとDSVを設定する
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, backBufferIndex);
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	dsvHandle.ptr += (0 * descriptorSizeDSV);

	commandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);

	// 青っぽい色。RGBAの順番で指定する
	float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };

	// 指定した色で画面全体をクリアする
	commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	// 指定した深度で画面全体をクリアする
	commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// 描画用のDescriptorHeapの設定
	ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
	commandList->SetDescriptorHeaps(1, descriptorHeaps);

	// Viewportを設定する
	commandList->RSSetViewports(1, &viewport);

	// Scissorを設定する
	commandList->RSSetScissorRects(1, &scissorRect);
}

// 描画後処理関数
void DirectXPlatform::PostDraw() {

	// これから書き込むバックバッファのインデックスを取得する
	UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

	// 画面に描く処理は全て終わり画面に映す準備ができたので、状態を遷移させる
	// 今回はRenderTargetからPresentにする
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

	// TransitionBarrierを張る
	commandList->ResourceBarrier(1, &barrier);

	// コマンドリストの内容を確定させる。全てのコマンドを積んでから、Closeすること
	HRESULT hr = commandList->Close();
	assert(SUCCEEDED(hr));

	//=====================
	// コマンドをキックする
	//=====================

#pragma region コマンドをキックする

	// GPUにコマンドリストを実行させる
	ID3D12CommandList* commandLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(1, commandLists);

	// GPUとOSに対して、画面の交換を行うように伝える
	swapChain->Present(1, 0);

	//=====================
	// GPUにSignalを送る
	//=====================

	// Fenceの値を更新
	fenceValue++;

	// GPUがここまで辿り着いた時に、Fenceの値を指定した値に代入するようにSignalを送る
	hr = commandQueue->Signal(fence.Get(), fenceValue);
	assert(SUCCEEDED(hr));

	// Fenceの値が指定したSignalの値に辿り着いているか確認する
	if (fence->GetCompletedValue() < fenceValue) {

		// 辿り着いていない場合は、Eventがシグナルされるまで待つ
		fence->SetEventOnCompletion(fenceValue, fenceEvent);

		// Eventを待つ
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	// FPS固定の更新
	UpdateFixFps();

	// 次フレーム用のコマンドリストを準備する
	hr = commandAllocator->Reset();
	assert(SUCCEEDED(hr));

	hr = commandList->Reset(commandAllocator.Get(), nullptr);
	assert(SUCCEEDED(hr));

#pragma endregion
}

// レンダーターゲットビューの初期化関数
void DirectXPlatform::RenderTargetViewInitialize() {

	// RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = kRtvFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	// ディスクリプタの先頭を取得する
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2]{};

	// 裏表の2つ分
	for (uint32_t i = 0; i < kBufferCount; ++i) {
		// 先頭ハンドルからインデックス分だけ進めたハンドルを計算する
		rtvHandles[i] = rtvStartHandle;
		rtvHandles[i].ptr += (i * descriptorSizeRTV);

		// 計算したハンドル位置にRTVを作成する
		device->CreateRenderTargetView(swapChainResources[i].Get(), &rtvDesc, rtvHandles[i]);
	}
}

// 深度ステンシルビューの初期化関数
void DirectXPlatform::DepthStencilViewInitialize() {

#pragma region DepthStencilViewの生成

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
	fenceValue = 0;
	HRESULT hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	// Fenceのシグナルを待つためのEventを生成する
	fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(fenceEvent != nullptr);

#pragma endregion

}

// ビューポート矩形の初期化関数
void DirectXPlatform::ViewportRectInitialize() {

	// Viewportの設定
	viewport.Width = static_cast<float>(WindowsApi::kClientWidth);
	viewport.Height = static_cast<float>(WindowsApi::kClientHeight);
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
}

// シザリング矩形の初期化関数
void DirectXPlatform::ScissorRectInitialize() {

	// シザー矩形の設定
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
	HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));

	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	// 現時点でincludeはしないが、includeに対応するための設定をしておく
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

// FPS固定初期化関数
void DirectXPlatform::InitializeFixFps() {

	// 現在時間を記録する
	mReference = std::chrono::steady_clock::now();

}

// FPS固定更新関数
void DirectXPlatform::UpdateFixFps() {

	// 1/60秒ピッタリの時間(1フレーム分の時間)
	const std::chrono::microseconds kMinTime(static_cast<int64_t>(1000000.0f / 60.0f));

	// 1/60秒よりわずかに短い時間
	const std::chrono::microseconds kMinCheckTime(static_cast<int64_t>(1000000.0f / 65.0f));

	// 現在時間を取得する
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

	// 前回記録した時間からの経過時間を計算する
	std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - mReference);

	// 1/60秒(よりわずかに短い時間)経っていない場合
	if (elapsed < kMinCheckTime) {
		// 1/60秒経過するまで微小なスリーブを繰り返す
		while (std::chrono::steady_clock::now() - mReference < kMinTime) {
			// 1マイクロ秒スリーブ
			std::this_thread::sleep_for(std::chrono::microseconds(1));
		}
	}

	// 現在時間を記録する
	mReference = std::chrono::steady_clock::now();
}