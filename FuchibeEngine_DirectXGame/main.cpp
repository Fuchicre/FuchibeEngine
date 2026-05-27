#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <fstream>
#include <chrono>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <dbgHelp.h>
#include <strsafe.h>
#include <dxgidebug.h>
#include <dxcapi.h>
#include "MathUtils.h"
#include "externals/DirectXTex/DirectXTex.h"
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")

//========================
// ウィンドウプロシージャ
//========================

#pragma region ウィンドウプロシージャ

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {

#ifdef USE_IMGUI
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
		return true;
	}
#endif

	// メッセージに応じてゲーム固有の処理を行う
	switch (msg) {

		// ウィンドウが破棄された
	case WM_DESTROY:

		// OSに対して、アプリの終了を伝える
		PostQuitMessage(0);

		return 0;
	}

	// 標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);

}

#pragma endregion

//=======================
// 関数群
//=======================

#pragma region 関数群

// Vector4構造体
struct Vector4 {
	float x, y, z, w;
};

// Vector2構造体
struct Vector2 {
	float x, y;
};

// 頂点データ
struct VertexData {
	Vector4 position;
	Vector2 texcoord;
};

// ログ出力用の関数
void Log(const std::string& message) {
	OutputDebugStringA(message.c_str());
}

// CoverString関数
std::wstring ConvertString(const std::string& str) {
	if (str.empty()) {
		return std::wstring();
	}

	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0) {
		return std::wstring();
	}
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}

std::string ConvertString(const std::wstring& str) {
	if (str.empty()) {
		return std::string();
	}

	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded == 0) {
		return std::string();
	}
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
}

void Log(std::ostream& os, const std::string& message) {
	os << message << std::endl;
	OutputDebugStringA(message.c_str());
}

// CrashHandlerの登録
static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {

	// 時刻を取得して、時刻を名前にしたファイルを作成する。Dumpディレクトリ以下に出力
	SYSTEMTIME time;
	GetLocalTime(&time);
	wchar_t filePath[MAX_PATH] = { 0 };
	CreateDirectory(L"./Dumps", nullptr);
	StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/%04d-%02d%02d-%02d%02d.dmp", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
	HANDLE dumpFileHandle = CreateFile(filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);

	// processId(このexeのId)とクラッシュ(例外)が発生したthreadIdを取得する
	DWORD processId = GetCurrentProcessId();
	DWORD threadId = GetCurrentThreadId();

	// 設定情報を入力
	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{ 0 };
	minidumpInformation.ThreadId = threadId;
	minidumpInformation.ExceptionPointers = exception;
	minidumpInformation.ClientPointers = TRUE;

	// Dumpを出力。miniDumpNormalは最低限の情報を出力するフラグ
	MiniDumpWriteDump(GetCurrentProcess(), processId, dumpFileHandle, MiniDumpNormal, &minidumpInformation, nullptr, nullptr);

	// 他に関連付けられているSEH例外ハンドラがあれば実行する。通常はプロセスを終了させる
	return EXCEPTION_EXECUTE_HANDLER;
}

// BufferResourceの作成関数
ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes) {

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
	ID3D12Resource* bufferResource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&bufferResource));
	assert(SUCCEEDED(hr));

	return bufferResource;

}

//===========================
// DescriptorHeapの作成関数
//===========================

ID3D12DescriptorHeap* CreateDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible) {

	ID3D12DescriptorHeap* descriptorHeap = nullptr;
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

//====================
// CompileShader関数
//====================

#pragma region CompileShader関数

IDxcBlob* CompileShader(
	const std::wstring& filePath,
	const wchar_t* profile,
	IDxcUtils* dxcUtils,
	IDxcCompiler3* dxcCompiler,
	IDxcIncludeHandler* includeHandler) {

	// これからシェーダーをコンパイルする旨をログに出力する
	Log(ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));

	// hlslファイルを読む
	IDxcBlobEncoding* shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);

	// 読めなかったら止める
	assert(SUCCEEDED(hr));

	// 読み込んだファイルの内容を設定する
	DxcBuffer shaderSourceBuffer;
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
	IDxcResult* shaderResult = nullptr;
	hr = dxcCompiler->Compile(&shaderSourceBuffer, arguments, _countof(arguments), includeHandler, IID_PPV_ARGS(&shaderResult));

	// コンパイルエラーではなくdxcが起動できないなどの根本的なエラーが発生した場合は止める
	assert(SUCCEEDED(hr));

	// 警告・エラーが出ていたらログに出力して止める
	IDxcBlobUtf8* shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);

	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		Log(shaderError->GetStringPointer());

		// 警告・エラーが出ている場合は止める
		assert(false);
	}

	// コンパイル結果から実行用のバイナリ部分を取得する
	IDxcBlob* shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));

	// 成功したログを出力する
	Log(ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));

	// もう使わないリソースを解放する
	shaderSource->Release();
	shaderResult->Release();

	// 実行用のバイナリを返す
	return shaderBlob;
}

#pragma	endregion

//===================================================
// LoadTexture関数(DirectXTexを使ってTextureを読む用)
//===================================================

#pragma region LoadTexture関数(DirectXTexを使ってTextureを読む用)

DirectX::ScratchImage LoadTexture(const std::string& filePath) {

	// テクスチャファイルを読んで、プログラムで扱えるようにする
	DirectX::ScratchImage image{};
	std::wstring filePathW = ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));

	// ミップマップの作成
	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));

	// ミップマップ付きのデータを返す
	return mipImages;
}

#pragma	endregion

//=========================================================================
// CreateTexture関数(読み込んだTexture情報を基にTextureResourceを作成する関数)
//=========================================================================

#pragma region CreateTexture関数(読み込んだTexture情報を基にTextureResourceを作成する関数)

ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metaData) {

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

	// 利用するHeapの設定。非常に特殊な運用。02_04_exで一般的なケース版がある
	D3D12_HEAP_PROPERTIES heapProperties{};

	// 細かい設定を行う
	heapProperties.Type = D3D12_HEAP_TYPE_CUSTOM;

	// WriteBackポリシーでCPUアクセス可能
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;

	// プロセッサの近くに配置
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;

	// Resourceを生成して、Returnする
	ID3D12Resource* resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));

	return resource;
}

#pragma endregion

//===========================================
// UplodeTextureData関数(データを転送する関数)
//===========================================

#pragma region UplodeTextureData関数(データを転送する関数)

void UplodeTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages) {

	// Meta情報を取得
	const DirectX::TexMetadata& metaData = mipImages.GetMetadata();

	// 全MipMapについて
	for (size_t mipLevel = 0; mipLevel < metaData.mipLevels; mipLevel++) {

		// MipMapLevelを指定して各Imageを取得
		const DirectX::Image* img = mipImages.GetImage(mipLevel, 0, 0);

		// Textureに転送
		HRESULT hr = texture->WriteToSubresource(UINT(mipLevel), nullptr, img->pixels, UINT(img->rowPitch), UINT(img->slicePitch));
		assert(SUCCEEDED(hr));
	}
}

#pragma endregion

#pragma endregion

//===============
// main関数
//===============

#pragma region main関数

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

	// COMの初期化
	assert(SUCCEEDED(CoInitializeEx(0, COINIT_MULTITHREADED)));

	// 誰も捕捉しなかった場合(Unhandled)に捕捉する関数を登録
	SetUnhandledExceptionFilter(ExportDump);

	//=====================
	// ウィンドウの生成
	//=====================

#pragma region ウィンドウの生成

	WNDCLASS wc{};

	// ウィンドウプロシージャ
	wc.lpfnWndProc = WindowProc;

	// ウィンドウクラス名(なんでも良い)
	wc.lpszClassName = L"FuchibeEngineWindowClass";

	// インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);

	// カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	// ウィンドウクラスを登録する
	RegisterClass(&wc);

	// クライアント領域のサイズ
	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;

	// ウィンドウサイズを表す構造体にクライアント領域を入れる
	RECT wrc = { 0, 0, kClientWidth, kClientHeight };

	// クライアント領域を元に実際のサイズにwrcを変更してもらう
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	//ウィンドウの生成
	HWND hwnd = CreateWindow(
		// 利用するクラス名
		wc.lpszClassName,
		// タイトルバーの文字
		L"FuchibeEngine",
		// よく見るウィンドウスタイル
		WS_OVERLAPPEDWINDOW,
		// 表示するX座標(Windowsに任せる)
		CW_USEDEFAULT,
		// 表示するY座標(WindowsOSに任せる)
		CW_USEDEFAULT,
		// ウィンドウの横幅
		wrc.right - wrc.left,
		// ウィンドウの縦幅
		wrc.bottom - wrc.top,
		// 親ウィンドウハンドル
		nullptr,
		// メニューハンドル
		nullptr,
		// インスタンスハンドル
		wc.hInstance,
		// オプション
		nullptr);

	// ウィンドウを表示する
	ShowWindow(hwnd, SW_SHOW);

#pragma endregion

	//==================
	// デバッグレイヤー
	//==================

#pragma region デバッグレイヤー

#ifdef _DEBUG

	ID3D12Debug1* debugController = nullptr;

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
	IDXGIFactory7* dxgiFactory = nullptr;

	// HRESULTはWindows系のエラーコードであり、関数が成功したかどうかをSUCCEEDEDマクロで判定できる
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));

	// 初期化の根本的な部分でエラーが発生した場合はプログラムが間違っているか、どうにもできない場合が多いので、assertで止める
	assert(SUCCEEDED(hr));

	// 使用するアダプタ用の変数。最初に nullptr で初期化しておく
	IDXGIAdapter4* useAdapter = nullptr;

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
			Log(ConvertString(std::format(L"Use Adapter:{}\n", adapterDesc.Description)));

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

	ID3D12Device* device = nullptr;

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
		hr = D3D12CreateDevice(useAdapter, featureLevels[i], IID_PPV_ARGS(&device));

		// 指定した機能レベルでデバイスが生成できたかを確認する
		if (SUCCEEDED(hr)) {
			Log(std::format("Feature Level:{}\n", featureLevelStrings[i]));
			break;
		}
	}

	// デバイスが生成できなかった場合は起動できないため、assertで止める
	assert(device != nullptr);

	// 初期化完了のログを出力する
	Log("Conplete create D3D12 Device!!!\n");

#pragma endregion

	//=======================
	// エラー及び警告での停止
	//=======================

#pragma region エラー及び警告での停止

#ifdef _DEBUG

	ID3D12InfoQueue* infoQueue = nullptr;

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

		// 解放
		infoQueue->Release();
	}

#endif

#pragma endregion

	// ログ出力用のディレクトリを作成する
	std::filesystem::create_directory("logs");

	//=======================
	// コマンドキューの生成
	//=======================

#pragma region コマンドキューの生成

	ID3D12CommandQueue* commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));

	// コマンドキューの生成が上手くいかなかったので、起動できない
	assert(SUCCEEDED(hr));

#pragma endregion

	//=======================
	// コマンドリストの生成
	//=======================

#pragma region コマンドリストの生成

	// コマンドアロケータの生成
	ID3D12CommandAllocator* commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

	// コマンドアロケータの生成が上手くいかなかったので、起動できない
	assert(SUCCEEDED(hr));

	// コマンドリストを生成する
	ID3D12GraphicsCommandList* commandList = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));

	// コマンドリストの生成が上手くいかなかったので、起動できない
	assert(SUCCEEDED(hr));

#pragma endregion

	//======================
	// スワップチェーンの生成
	//======================

#pragma region スワップチェーンの生成

	IDXGISwapChain4* swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};

	// 画面の幅。ウィンドウのクライアント領域を同じサイズにしておく
	swapChainDesc.Width = kClientWidth;

	// 画面の高さ。ウィンドウのクライアント領域を同じサイズにしておく
	swapChainDesc.Height = kClientHeight;

	// 色の形式
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	// マルチサンプルしない
	swapChainDesc.SampleDesc.Count = 1;

	// 描画のターゲットとして利用する
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

	// ダブルバッファ
	swapChainDesc.BufferCount = 2;

	// モニタに映したら、中身を破棄する
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	// コマンドキュー、ウィンドウハンドル、スワップチェーンの設定を渡して生成する
	hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(&swapChain));
	assert(SUCCEEDED(hr));

#pragma endregion

	//==========================
	// RTVDescriptorHeapの生成
	//==========================

#pragma region RTVDescriptorHeapの生成

	// RTV用のヒープでディスクリプタヒープの数は2。RTVはShader内で触るものではないので、shaderVisibleはfalse
	ID3D12DescriptorHeap* rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);

	// SwapChainからResourceを引っ張ってくる
	ID3D12Resource* swapChainResources[2] = { nullptr };
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));

	// Resourceを取得できなかったので、起動できない
	assert(SUCCEEDED(hr));

	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	assert(SUCCEEDED(hr));

#pragma endregion

	// SRV用のヒープでディスクリプタヒープの数は128。SRVはShader内で触るものなので、shaderVisibleはtrue
	ID3D12DescriptorHeap* srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	//==================
	// RTVの作成
	//==================

#pragma region RTVの作成

	// RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};

	// 出力結果をSRGBに変換して書き込む
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	// 2Dテクスチャとして書き込む
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	// ディスクリプタの先頭を取得する
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

	// RTVを2つ作るので、ディスクリプタを2つ用意する
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2]{};

	// まず1つ目のRTVを作る。1つ目は最初のところに作る。作る場所をこちらで指定する必要がある
	rtvHandles[0] = rtvStartHandle;
	device->CreateRenderTargetView(swapChainResources[0], &rtvDesc, rtvHandles[0]);

	// 2つ目のディスクリプタハンドルを得る(自力で)
	rtvHandles[1].ptr = rtvHandles[0].ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// 2つ目のRTVを作る
	device->CreateRenderTargetView(swapChainResources[1], &rtvDesc, rtvHandles[1]);

#pragma endregion

	//==============================
	// 現在時刻でのログファイルの作成
	//==============================

#pragma region 現在時刻でのログファイルの作成

	// 現在時刻を取得 (UTC時刻)
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

	// ログファイルの名前にコンマ何秒はいらないので、削って秒にする
	std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>

		nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);

	// 日本時間(PCの設定時間)に変換
	std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };

	// formatを使って年月日_時分秒の文字列に変換
	std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);

	// 時刻を使ってファイル名を決定
	std::string logFilePath = std::string("logs/") + dateString + ".log";

	// ファイルを作って書き込み準備
	std::ofstream logStream(logFilePath);

#pragma endregion

	// ループに入る前に1回出す
	Log(logStream, "Game Engine Started.");

	//======================
	// FenceとEventの生成
	//======================

#pragma region FenceとEventの生成

	// 初期値0のFenceを生成する
	ID3D12Fence* fence = nullptr;
	uint64_t fenceValue = 0;
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	// Fenceのシグナルを待つためのEventを生成する
	HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent != nullptr);

#pragma endregion

	//==============
	// DXCの初期化
	//==============

#pragma region DXCの初期化

	// dxcompilerを初期化
	IDxcUtils* dxcUtils = nullptr;
	IDxcCompiler3* dxcCompiler = nullptr;

	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));

	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	// 現時点でincludeはしないが、includeに対応するための設定をしておく
	IDxcIncludeHandler* dxcIncludeHandler = nullptr;
	hr = dxcUtils->CreateDefaultIncludeHandler(&dxcIncludeHandler);
	assert(SUCCEEDED(hr));

#pragma endregion

	//======================================================
	// RootSignatureとDescriptorRangeとRootParameterの生成
	//======================================================

#pragma region RootSignatureとDescriptorRangeとRootParameterの生成

	// RootSignatureの生成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// DescriptorRangeの設定
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};

	// 0から始まる
	descriptorRange[0].BaseShaderRegister = 0;

	// 数は1つ
	descriptorRange[0].NumDescriptors = 1;

	// SRVを使う
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

	// Offsetを自動で計算
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// RootParameterの生成
	// PixelShaderのMaterialとVertexShaderのTransformとSRV
	D3D12_ROOT_PARAMETER rootParameters[3] = {};

	// CBVを使う
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;

	// PixelShaderを使う
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// レジスタ番号0とバインドする
	rootParameters[0].Descriptor.ShaderRegister = 0;

	// CBVを使う
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;

	// VertexShaderで使う
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// レジスタ番号0を使う
	rootParameters[1].Descriptor.ShaderRegister = 0;

	// DescriptorTableを使う
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;

	// PixelShaderで使う
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// Tableの中身の配列を指定
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;

	// Tableで利用する数
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	// ルートパラメータ配列へのポインタ
	descriptionRootSignature.pParameters = rootParameters;

	// 配列の長さ
	descriptionRootSignature.NumParameters = _countof(rootParameters);

	//================
	// Samplerの設定
	//================

	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};

	// バイ二リアフィルタ
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

	// 0 ~ 1の範囲外をリピート
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

	// 比較しない
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;

	// ありったけのMipMapを使う
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;

	// レジスタ番号0を使う
	staticSamplers[0].ShaderRegister = 0;

	// PixelShaderで使う
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	// シリアライズしてバイナリにする
	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);

	if (FAILED(hr)) {
		if (errorBlob) {
			Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
			assert(false);
		}
	}

	// バイナリを元に生成
	ID3D12RootSignature* rootSignature = nullptr;
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));

#pragma endregion

	//====================
	// InputLayoutの設定
	//====================

#pragma region InputLayoutの設定

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[2] = {};

	// 頂点の位置。シェーダー側のSemanticはPOSITION
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TexCoord";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

#pragma endregion

	//====================
	// BlendStateの設定
	//====================

#pragma region BlendStateの設定

	D3D12_BLEND_DESC blendDesc{};

	// 全ての色要素を書きこむ
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

#pragma endregion

	//========================
	// RasterizerStateの設定
	//========================

#pragma region RasterizerStateの設定

	D3D12_RASTERIZER_DESC rasterizerDesc{};

	// 画面(時計回り)を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;

	// 三角形の内側を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

#pragma endregion

	//===========================
	// シェーダーをコンパイルする
	//===========================

#pragma region シェーダーをコンパイルする

	IDxcBlob* vertexShaderBlob = CompileShader(L"Object3d.VS.hlsl", L"vs_6_0", dxcUtils, dxcCompiler, dxcIncludeHandler);
	assert(vertexShaderBlob != nullptr);

	IDxcBlob* pixelShaderBlob = CompileShader(L"Object3d.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, dxcIncludeHandler);
	assert(pixelShaderBlob != nullptr);

#pragma endregion

	//=============
	// PSOの生成
	//=============

#pragma region PSOの生成

	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPilelineStateDesc{};

	// RootSignature
	graphicsPilelineStateDesc.pRootSignature = rootSignature;

	// InputLayout
	graphicsPilelineStateDesc.InputLayout = inputLayoutDesc;

	// VertexShader
	graphicsPilelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };

	// PixelShader
	graphicsPilelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };

	// BlendState
	graphicsPilelineStateDesc.BlendState = blendDesc;

	// RasterizerState
	graphicsPilelineStateDesc.RasterizerState = rasterizerDesc;

	// 書き込むRTVの情報
	graphicsPilelineStateDesc.NumRenderTargets = 1;
	graphicsPilelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	// 利用するト゚ポロジ(形状)のタイプを三角形にする
	graphicsPilelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// どのように画面に色を打ち込むを設定する。今回は何も特殊なことをしないので、デフォルトのままにする
	graphicsPilelineStateDesc.SampleDesc.Count = 1;
	graphicsPilelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// 実際に生成する
	ID3D12PipelineState* graphicsPipelineState = nullptr;
	hr = device->CreateGraphicsPipelineState(&graphicsPilelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));

#pragma endregion

	//=======================
	// VertexResourceの生成
	//=======================

#pragma region VertexResourceの生成

	// 頂点リソースの生成
	ID3D12Resource* vertexResource = CreateBufferResource(device, sizeof(VertexData) * 3);

#pragma endregion

	//=========================
	// MaterialResourceの生成
	//=========================

#pragma region MaterialResourceの生成

	// マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	ID3D12Resource* materialResource = CreateBufferResource(device, sizeof(Vector4));

	// マテリアルにデータを書き込む
	Vector4* materialData = nullptr;

	// 書き込むためのアドレスを取得する
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));

	// 白色にする
	*materialData = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

#pragma endregion

	//================================
	// TransformMatrixResourceの生成
	//================================

#pragma region TransformMatrixResourceの生成

	// WVP用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	ID3D12Resource* wvpResource = CreateBufferResource(device, sizeof(Matrix4x4));

	// データを書き込む
	Matrix4x4* wvpData = nullptr;

	// 書き込むためのアドレスを取得
	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));

	// 単位行列を書き込んでおく
	*wvpData = MathUtils::MakeIdentity4x4();

#pragma endregion

	//===========================
	// VertexBafferViewの設定
	//===========================

#pragma region VertexBafferViewの設定

	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};

	// リソースの先頭アドレスから使う
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();

	// 使用するリソースのサイズは頂点3つ分のサイズ
	vertexBufferView.SizeInBytes = sizeof(VertexData) * 3;

	// 1頂点あたりのサイズ
	vertexBufferView.StrideInBytes = sizeof(VertexData);

#pragma endregion

	//================================
	// Resourceに頂点データを書きこむ
	//================================

#pragma region Resourceに頂点データを書きこむ

	// 頂点リソースにデータを書き込む
	VertexData* vertexData = nullptr;

	// 書き込むためのアドレスを取得する
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

	// 左下
	vertexData[0].position = { -0.5f, -0.5f, 0.0f, 1.0f };
	vertexData[0].texcoord = { 0.0f, 1.0f };

	// 上
	vertexData[1].position = { 0.0f, 0.5f, 0.0f, 1.0f };
	vertexData[1].texcoord = { 0.5f, 0.0f };

	// 右下
	vertexData[2].position = { 0.5f, -0.5f, 0.0f, 1.0f };
	vertexData[2].texcoord = { 1.0f, 1.0f };

#pragma endregion

	//==================================
	// ViewportとScissorRectの設定
	//==================================

#pragma region ViewportとScissorRectの設定

	// Viewportの設定
	D3D12_VIEWPORT viewport{};

	// クライアント領域のサイズと同じにして、画面全体に表示する
	viewport.Width = kClientWidth;
	viewport.Height = kClientHeight;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	// シザー矩形の設定
	D3D12_RECT scissorRect{};

	// 基本的にビューポートと同じ矩形が構成されるようにする
	scissorRect.left = 0;
	scissorRect.right = kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = kClientHeight;

#pragma endregion

	//=======================
	// Transform変数の作成
	//=======================

#pragma region Transform変数の作成

	Transform transform{ {1.0f, 1.0f, 1.0f},{0.0f, 0.0f, 0.0f},{0.0f, 0.0f, 0.0f} };

#pragma endregion

	//================
	// ImGuiの初期化
	//================

#pragma region ImGuiの初期化

#ifdef USE_IMGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(device, swapChainDesc.BufferCount, rtvDesc.Format, srvDescriptorHeap, srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();
#endif

#pragma endregion

	//============================
	// Textureを読み込んで転送する
	//============================

#pragma region Textureを読み込んで転送する

	DirectX::ScratchImage mipImages = LoadTexture("resources/uvChecker.png");
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();

	// リソース作成
	ID3D12Resource* textureResource = CreateTextureResource(device, metadata);

	// データ転送
	UplodeTextureData(textureResource, mipImages);

#pragma endregion

	//==========================
	// SRVDescriptorHeapの生成
	//==========================

#pragma region SRVDescriptorHeapの生成

	// metaDataを基にSRVを作成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// 2Dテクスチャ
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	// SRVを作成するDescriptorHeapの場所を決める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

	// 先頭はImGuiが使っているのでその次を使う
	textureSrvHandleCPU.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	textureSrvHandleGPU.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// SRVを作成
	device->CreateShaderResourceView(textureResource, &srvDesc, textureSrvHandleCPU);

#pragma endregion

	//=====================
	// メインループ
	//=====================

#pragma region メインループ

	MSG msg{};

	// ウィンドウの×ボタンが押されるまでループ
	while (msg.message != WM_QUIT) {

		// ウィンドウにメッセージが来たら、最優先で処理する
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			// メッセージがある場合は、翻訳して、ウィンドウプロシージャに渡す
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {

#ifdef USE_IMGUI
			// ImGuiを使う
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
#endif

			//================
			// ゲームの処理
			//================

#pragma region ゲームの処理

			// 開発用UIの処理。実際に開発用のUIを出す場合は、ここをゲーム固有の処理に置き換える
#ifdef USE_IMGUI
			ImGui::Begin("Material Settings");

			// 直感的なカラーホイールのためのオプションフラグ
			ImGuiColorEditFlags flags =
				ImGuiColorEditFlags_AlphaBar |
				ImGuiColorEditFlags_PickerHueBar |
				ImGuiColorEditFlags_DisplayRGB |
				ImGuiColorEditFlags_DisplayHex;

			// 色編集用のImGui
			ImGui::ColorPicker4("Color Selector", &materialData->x, flags);

			ImGui::End();
#endif

			Transform cameraTransform{ {1.0f, 1.0f, 1.0f},{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -5.0f} };

			// Transformの更新
			transform.rotate.y += 0.03f;

			// WorldMatrixを作成
			Matrix4x4 worldMatrix = MathUtils::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);

			Matrix4x4 cameraMatrix = MathUtils::MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);

			Matrix4x4 viewMatrix = MathUtils::Inverse(cameraMatrix);

			// 透視投影行列の作成
			Matrix4x4 projectionMatrix = MathUtils::MakePerspectiveFovMatrix(0.45f, static_cast<float>(kClientWidth) / static_cast<float>(kClientHeight), 0.1f, 100.0f);

			// wvpMatrixを作成する
			Matrix4x4 matWV = MathUtils::Multiply(worldMatrix, viewMatrix);
			Matrix4x4 worldViewProjectionMatrix = MathUtils::Multiply(matWV, projectionMatrix);

			// CBufferの中身を更新
			*wvpData = worldViewProjectionMatrix;

			// ImGuiの内部コマンドを生成する
#ifdef USE_IMGUI
			ImGui::Render();
#endif

			//===============================
			// コマンドを積みこんで確定させる
			//===============================

#pragma region コマンドを積みこんで確定させる

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
			barrier.Transition.pResource = swapChainResources[backBufferIndex];

			// 遷移前(現在)のResourceState
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;

			// 遷移後のResourceState
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

			// TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

#pragma endregion

			// 描画先のRTVを設定する
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, nullptr);

			// 青っぽい色。RGBAの順番で指定する
			float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };

			// 指定した色で画面全体をクリアする
			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);

			// 描画用のDescriptorHeapの設定
			ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap };
			commandList->SetDescriptorHeaps(1, descriptorHeaps);

			// Viewportを設定する
			commandList->RSSetViewports(1, &viewport);

			// Scissorを設定する
			commandList->RSSetScissorRects(1, &scissorRect);

			// RootSignatureを設定する。PSOにも設定しているが、別途設定が必要
			commandList->SetGraphicsRootSignature(rootSignature);

			// PSOを設定する
			commandList->SetPipelineState(graphicsPipelineState);

			// VBVを設定する
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);

			// トポロジ(形状)を設定する。PSOに設定しているものとはまた別。同じものを設定すると考えておくといい
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			// CBVを設定する(マテリアルCBufferの場所を設定)
			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());

			// WVP用のCBVを設定する
			commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());

			// SRVのDescriptorTableの先頭を設定。2はrootParameters[2]である
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);

			// 描画コマンド!(DrawCall)頂点3つで1つのインスタンス
			commandList->DrawInstanced(3, 1, 0, 0);

			// 実際のcommandListのImGui描画コマンドを積む
#ifdef USE_IMGUI
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif

			// 画面に描く処理は全て終わり画面に映す準備ができたので、状態を遷移させる
			// 今回はRenderTargetからPresentにする
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

			// TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

			// コマンドリストの内容を確定させる。全てのコマンドを積んでから、Closeすること
			hr = commandList->Close();

#pragma endregion

			//=====================
			// コマンドをキックする
			//=====================

#pragma region コマンドをキックする

			// GPUにコマンドリストを実行させる
			ID3D12CommandList* commandLists[] = { commandList };
			commandQueue->ExecuteCommandLists(1, commandLists);

			// GPUとOSに対して、画面の交換を行うように伝える
			swapChain->Present(1, 0);

			//=====================
			// GPUにSignalを送る
			//=====================

			// Fenceの値を更新
			fenceValue++;

			// GPUがここまで辿り着いた時に、Fenceの値を指定した値に代入するようにSignalを送る
			hr = commandQueue->Signal(fence, fenceValue);

			// Fenceの値が指定したSignalの値に辿り着いているか確認する
			if (fence->GetCompletedValue() < fenceValue) {

				// 辿り着いていない場合は、Eventがシグナルされるまで待つ
				fence->SetEventOnCompletion(fenceValue, fenceEvent);

				// Eventを待つ
				WaitForSingleObject(fenceEvent, INFINITE);
			}

			// 次フレーム用のコマンドリストを準備する
			hr = commandAllocator->Reset();
			assert(SUCCEEDED(hr));

			hr = commandList->Reset(commandAllocator, nullptr);
			assert(SUCCEEDED(hr));

#pragma endregion

#pragma endregion

		}
	}

#pragma endregion

	// 終了時に記録する
	Log(logStream, "Game Engine Terminated.");

	// リソースリークチェック
	IDXGIDebug1* debug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		debug->Release();
	}

	//===========
	// 解放処理
	//===========

#pragma region 解放処理

	// ImGuiの終了処理
#ifdef USE_IMGUI
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif

	// 各種イベント・フェンスの解放
	CloseHandle(fenceEvent);
	fence->Release();

	// パイプライン・シェーダー関連リソースの解放
	graphicsPipelineState->Release();
	rootSignature->Release();

	if (signatureBlob) {
		signatureBlob->Release();
	}

	if (errorBlob) {
		errorBlob->Release();
	}

	pixelShaderBlob->Release();
	vertexShaderBlob->Release();

	// バッファ・マテリアルリソースの解放
	wvpResource->Release();
	materialResource->Release();
	vertexResource->Release();

	// ディスクリプタヒープの解放
	srvDescriptorHeap->Release();
	rtvDescriptorHeap->Release();

	// スワップチェーン関連の解放
	if (swapChainResources[0]) { swapChainResources[0]->Release(); }
	if (swapChainResources[1]) { swapChainResources[1]->Release(); }
	swapChain->Release();

	mipImages.Release();

	// コマンド関連・デバイスの解放
	commandList->Release();
	commandAllocator->Release();
	commandQueue->Release();

	useAdapter->Release();
	dxgiFactory->Release();

#ifdef _DEBUG
	if (debugController) {
		debugController->Release();
	}
#endif

	// デバイスのReleaseが全て終わった後にデバイス本体を解放する
	device->Release();

	// ウィンドウを閉じる
	CloseWindow(hwnd);

#pragma endregion

	// COMの終了処理
	CoUninitialize();

	return 0;
}

#pragma endregion