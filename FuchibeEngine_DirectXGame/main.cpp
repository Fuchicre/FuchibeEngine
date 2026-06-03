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
#include "externals/DirectXTex/d3dx12.h"
#include <vector>
#include "Easing.h"
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

//===========================
// BufferResourceの作成関数
//===========================

#pragma region BufferResourceの作成関数

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

#pragma endregion

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
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// WriteBackポリシーでCPUアクセス可能
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;

	// プロセッサの近くに配置
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

	// Resourceを生成して、Returnする
	ID3D12Resource* resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));

	return resource;
}

#pragma endregion

//===========================================
// UploadTextureData関数(データを転送する関数)
//===========================================

#pragma region UploadTextureData関数(データを転送する関数)

[[nodiscard]] ID3D12Resource* UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages, ID3D12Device* device, ID3D12GraphicsCommandList* commandList) {

	// 中間リソース
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;

	// PrepareUploadを利用して、読み込んだデータからDirectX12用のSubresourceの配列を作成する
	DirectX::PrepareUpload(device, mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);

	// Subresourceの数を基に、コピー元となるintermediateResourceに必要なサイズを計算する
	uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));

	// 計算したサイズでintermediateResourceを作成する
	ID3D12Resource* intermediateResource = CreateBufferResource(device, intermediateSize);

	// データ転送をコマンドに積む
	UpdateSubresources(commandList, texture, intermediateResource, 0, 0, UINT(subresources.size()), subresources.data());

	// intermediateResourceを返す
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;

	commandList->ResourceBarrier(1, &barrier);
	return intermediateResource;

}

#pragma endregion

//================================
// CreateDepthStencilTexture関数
//================================

#pragma region DepthStencilTexture関数

ID3D12Resource* CreateDepthStencilTexture(ID3D12Device* device, int32_t width, int32_t height) {

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
	ID3D12Resource* resource = nullptr;

	HRESULT hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));

	return resource;

}

#pragma endregion

//==============================
// DescriptorCPUHandle取得関数
//==============================

#pragma region DescriptorCPUHandle取得関数

D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) {

	// ディスクリプタヒープの先頭のハンドルを取得する
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();

	// index分だけハンドルを進める
	handleCPU.ptr += (descriptorSize * index);

	return handleCPU;
}

#pragma endregion

//==============================
// DescriptorGPUHandle取得関数
//==============================

#pragma region DescriptorGPUHandle取得関数

D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) {

	// ディスクリプタヒープの先頭のハンドルを取得する
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();

	// index分だけハンドルを進める
	handleGPU.ptr += (descriptorSize * index);

	return handleGPU;
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

	//==============================
	// DepthStencilResourceの生成
	//==============================

#pragma region DepthStencilResourceの生成

	// ウィンドウと同じサイズで生成
	ID3D12Resource* depthStencilResource = CreateDepthStencilTexture(device, kClientWidth, kClientHeight);

#pragma endregion

	//================================
	// 全てのディスクリプタサイズの定義
	//================================

#pragma region 全てのディスクリプタサイズの定義

	const uint32_t descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const uint32_t descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

#pragma endregion

	//==========================
	// DepthStencilViewの作成
	//==========================

#pragma region DepthStencilViewの作成

	// DSV用のHeapでディスクリプタの数は1。DSVはShaderないで触るものではないので、ShaderVisibleはfalseにする
	ID3D12DescriptorHeap* dsvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

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
	device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvHandleTarget);

#pragma endregion

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

	// 2つ目のディスクリプタハンドルを得る
	rtvHandles[1].ptr = rtvHandles[0].ptr + descriptorSizeRTV;

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

	// ループに入る前に1回出す
	Log(logStream, "Game Engine Started.");

#pragma endregion

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

	//==========================
	// DepthStencilStateの設定
	//==========================

#pragma region DepthStencilStateの設定

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};

	// Depthの機能を有効化する
	depthStencilDesc.DepthEnable = true;

	// 書き込みをする
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

	// 比較関数はLessEqual。つまり、近ければ描画される
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

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

	// DepthStencilの設定
	graphicsPilelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPilelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

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

	//================
	// 演出専用の変数
	//================

	// 演出用ピクセルシェーダーバイナリ
	IDxcBlob* effectPixelShaderBlob = nullptr;

	// ブレンドモードの種類を定義する列挙型
	enum EffectBlendMode {
		// ブレンドなし
		BlendMode_None,
		// 通常半透明
		BlendMode_Alpha,
		// 加算合成
		BlendMode_Add,
		// 減算合成
		BlendMode_Subtract,
		// スクリーン合成
		BlendMode_Screen,
		// 総数
		BlendMode_Count
	};

	// 現在選択されているブレンドモード(初期状態はNone)
	int currentBlendIndex = BlendMode_None;

	// 初期値は0:Linear(イージングなし)
	int currentEasingType = 0;

	// 演出用パイプライン状態オブジェクトを配列で管理する
	ID3D12PipelineState* effectPipelineStates[BlendMode_Count] = { nullptr };

	//============================================
	// 演出専用ピクセルシェーダーのコンパイルとPSOの生成
	//============================================

	// 提示されたCompileShaderのルールに合わせて呼び出し
	effectPixelShaderBlob = CompileShader(L"Effect3D.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, dxcIncludeHandler);
	assert(effectPixelShaderBlob != nullptr);

	// 通常のDescをベースにして演出用Descを作成する
	D3D12_GRAPHICS_PIPELINE_STATE_DESC effectPipelineStateDesc = graphicsPilelineStateDesc;

	// ピクセルシェーダーのみ演出用のBlobに差し替える
	effectPipelineStateDesc.PS = { effectPixelShaderBlob->GetBufferPointer(), effectPixelShaderBlob->GetBufferSize() };

	// 演出用はすべて共通で深度バッファの書き込みを禁止にする(半透明の重なりを綺麗にするため)
	effectPipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	effectPipelineStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// 【None:ブレンドなし】用のPSOの生成
	effectPipelineStateDesc.BlendState.RenderTarget[0].BlendEnable = false;
	effectPipelineStateDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	hr = device->CreateGraphicsPipelineState(&effectPipelineStateDesc, IID_PPV_ARGS(&effectPipelineStates[BlendMode_None]));
	assert(SUCCEEDED(hr));

	// 【Alpha:通常半透明(アルファブレンド)】用のPSOの生成
	effectPipelineStateDesc.BlendState.RenderTarget[0].BlendEnable = true;
	effectPipelineStateDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	effectPipelineStateDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	effectPipelineStateDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	effectPipelineStateDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	effectPipelineStateDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	effectPipelineStateDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

	hr = device->CreateGraphicsPipelineState(&effectPipelineStateDesc, IID_PPV_ARGS(&effectPipelineStates[BlendMode_Alpha]));
	assert(SUCCEEDED(hr));


	// 【Add:加算合成】用のPSOの生成
	effectPipelineStateDesc.BlendState.RenderTarget[0].BlendEnable = true;
	effectPipelineStateDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	effectPipelineStateDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	effectPipelineStateDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	effectPipelineStateDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	effectPipelineStateDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	effectPipelineStateDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

	hr = device->CreateGraphicsPipelineState(&effectPipelineStateDesc, IID_PPV_ARGS(&effectPipelineStates[BlendMode_Add]));
	assert(SUCCEEDED(hr));

	// 【Subtract:減算合成】用のPSOの生成
	effectPipelineStateDesc.BlendState.RenderTarget[0].BlendEnable = true;
	effectPipelineStateDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	effectPipelineStateDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	effectPipelineStateDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
	effectPipelineStateDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	effectPipelineStateDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	effectPipelineStateDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

	hr = device->CreateGraphicsPipelineState(&effectPipelineStateDesc, IID_PPV_ARGS(&effectPipelineStates[BlendMode_Subtract]));
	assert(SUCCEEDED(hr));

	// 【Screen:スクリーン合成】用のPSOの生成
	effectPipelineStateDesc.BlendState.RenderTarget[0].BlendEnable = true;

	effectPipelineStateDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
	effectPipelineStateDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	effectPipelineStateDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	effectPipelineStateDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	effectPipelineStateDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	effectPipelineStateDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

	hr = device->CreateGraphicsPipelineState(&effectPipelineStateDesc, IID_PPV_ARGS(&effectPipelineStates[BlendMode_Screen]));
	assert(SUCCEEDED(hr));

#pragma endregion

	//=======================
	// VertexResourceの生成
	//=======================

#pragma region VertexResourceの生成

	// 三角形の頂点リソースの生成
	ID3D12Resource* vertexResource = CreateBufferResource(device, sizeof(VertexData) * 6);

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
	// TransformationMatrixResourceの生成
	//================================

#pragma region TransformationMatrixResourceの生成

	// 1枚目の三角形のWVP用のリソースを作る
	ID3D12Resource* wvpResource1 = CreateBufferResource(device, sizeof(Matrix4x4));
	Matrix4x4* wvpData1 = nullptr;
	wvpResource1->Map(0, nullptr, reinterpret_cast<void**>(&wvpData1));
	*wvpData1 = MathUtils::MakeIdentity4x4();

	// 2枚目の三角形のWVP用のリソースを作る
	ID3D12Resource* wvpResource2 = CreateBufferResource(device, sizeof(Matrix4x4));
	Matrix4x4* wvpData2 = nullptr;
	wvpResource2->Map(0, nullptr, reinterpret_cast<void**>(&wvpData2));
	*wvpData2 = MathUtils::MakeIdentity4x4();

	// 演出用の三角形の数
	const int kMaxEffectObjects = 100;

	// 演出用のSRTデータを保持する配列
	Transform effectTransforms[kMaxEffectObjects];
	for (int i = 0; i < kMaxEffectObjects; i++) {
		effectTransforms[i].scale = { 1.0f, 1.0f, 1.0f };
		effectTransforms[i].rotate = { 0.0f, 0.0f, 0.0f };
		effectTransforms[i].translate = { -1.5f + i * 1.5f, 0.0f, 0.0f };
	}

	// 演出用のリソースとデータポインタの配列
	ID3D12Resource* effectWvpResources[kMaxEffectObjects] = { nullptr };
	Matrix4x4* effectWvpData[kMaxEffectObjects] = { nullptr };

	// タイマー変数
	float effectTimer = 0.0f;

	// 演出用の最大数分、定数バッファを作成するループ
	for (int i = 0; i < kMaxEffectObjects; i++) {

		effectWvpResources[i] = CreateBufferResource(device, sizeof(Matrix4x4));

		// 作成したリソースをMapして、いつでもCPUからデータを書き込めるようにする
		effectWvpResources[i]->Map(0, nullptr, reinterpret_cast<void**>(&effectWvpData[i]));
	}

#pragma endregion

	//===========================
	// VertexBafferViewの設定
	//===========================

#pragma region VertexBafferViewの設定

	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};

	// リソースの先頭アドレスから使う
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();

	// 使用するリソースのサイズは頂点6つ分のサイズ
	vertexBufferView.SizeInBytes = sizeof(VertexData) * 6;

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

	// 左下1
	vertexData[0].position = { -0.5f, -0.5f, 0.0f, 1.0f };
	vertexData[0].texcoord = { 0.0f, 1.0f };

	// 上1
	vertexData[1].position = { 0.0f, 0.5f, 0.0f, 1.0f };
	vertexData[1].texcoord = { 0.5f, 0.0f };

	// 右下1
	vertexData[2].position = { 0.5f, -0.5f, 0.0f, 1.0f };
	vertexData[2].texcoord = { 1.0f, 1.0f };

	// 2枚目の三角形 //

	// 左下2
	vertexData[3].position = { -0.5f, -0.5f, 0.5f, 1.0f };
	vertexData[3].texcoord = { 0.0f, 1.0f };

	// 上2
	vertexData[4].position = { 0.0f, 0.0f, 0.0f, 1.0f };
	vertexData[4].texcoord = { 0.5f, 0.0f };

	// 右下2
	vertexData[5].position = { 0.5f, -0.5f, -0.5f, 1.0f };
	vertexData[5].texcoord = { 1.0f, 1.0f };

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

	//==========================
	// 各オブジェクトの初期化
	//==========================

	// 1枚目の三角形用
	Transform transform1{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
	bool isAlive1 = true;
	int textureIndex1 = 0;

	// 2枚目の三角形用
	Transform transform2{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };
	bool isAlive2 = true;
	int textureIndex2 = 0;

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

	// (uvChecker.png)のTextureを読んで、転送する
	DirectX::ScratchImage mipImages = LoadTexture("resources/uvChecker.png");
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();

	// リソース作成
	ID3D12Resource* textureResource = CreateTextureResource(device, metadata);

	// (monsterBall.png)のTextureを読んで、転送する
	DirectX::ScratchImage mipImages2 = LoadTexture("resources/monsterBall.png");
	const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();

	// リソース作成
	ID3D12Resource* textureResource2 = CreateTextureResource(device, metadata2);

	//===========================
	// コマンドを実行して完了を待つ
	//===========================

#pragma region コマンドを実行して完了を待つ

	// 転送関数を呼び出し、コピーコマンドをコマンドリストに積む(中間リソースが戻る)
	ID3D12Resource* intermediateResource = UploadTextureData(textureResource, mipImages, device, commandList);
	ID3D12Resource* intermediateResource2 = UploadTextureData(textureResource2, mipImages2, device, commandList);

	// CommandListをCloseし、commandQueue->ExecuteCommandListsを使いキックする
	hr = commandList->Close();
	assert(SUCCEEDED(hr));

	ID3D12CommandList* initCommandLists[] = { commandList };
	commandQueue->ExecuteCommandLists(1, initCommandLists);

	// GPU側の実行が完了するのをフェンスを使って待つ
	fenceValue++;
	hr = commandQueue->Signal(fence, fenceValue);
	assert(SUCCEEDED(hr));

	if (fence->GetCompletedValue() < fenceValue) {
		fence->SetEventOnCompletion(fenceValue, fenceEvent);
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	// 実行が完了したので、allocatorとcommandListをResetして次のコマンドを積めるようにする
	hr = commandAllocator->Reset();
	assert(SUCCEEDED(hr));

	hr = commandList->Reset(commandAllocator, nullptr);
	assert(SUCCEEDED(hr));

	// 転送が終わったので、不要になった中間リソースとCPUメモリを安全に解放する
	intermediateResource->Release();
	mipImages.Release();

	intermediateResource2->Release();
	mipImages2.Release();

#pragma endregion

#pragma endregion

	//==========================
	// SRVDescriptorHeapの生成
	//==========================

#pragma region SRVDescriptorHeapの生成

	//===========================================
	// (uvChecker.png)のSRVを作成
	//===========================================

	// metaDataを基にSRVを作成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// 2Dテクスチャ
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

	// テクスチャリソースが持っているすべてのミップマップレベルを自動的にすべて割り当てる(符号なし整数の最大値を直接表す「0xFFFFFFFF」に書き換える)
	srvDesc.Texture2D.MipLevels = 0xFFFFFFFF;

	// SRVを作成するDescriptorHeapの場所を決める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

	// 先頭はImGuiが使っているのでその次を使う
	textureSrvHandleCPU.ptr += descriptorSizeSRV;
	textureSrvHandleGPU.ptr += descriptorSizeSRV;

	// 1枚目のSRVを作成(インデックス1の場所に書き込まれる)
	device->CreateShaderResourceView(textureResource, &srvDesc, textureSrvHandleCPU);

	//===========================================
	// (monsterBall.png)のSRVを作成
	//===========================================

	// 2枚目(monsterBall)のSRVもその隣(インデックス2)に作成する
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = textureSrvHandleCPU;
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = textureSrvHandleGPU;

	// さらに1個分(インデックス2の位置へ)ポインタを進める
	textureSrvHandleCPU2.ptr += descriptorSizeSRV;
	textureSrvHandleGPU2.ptr += descriptorSizeSRV;

	// srvDescのフォーマットを2枚目のものに合わせてSRVを作成する
	srvDesc.Format = metadata2.format;
	device->CreateShaderResourceView(textureResource2, &srvDesc, textureSrvHandleCPU2);

#pragma endregion

	//============================
	// スプライトの位置を保持する変数
	//============================

#pragma region スプライトの位置を保持する変数

	// カメラのTransform
	Transform cameraTransform{ {1.0f, 1.0f, 1.0f},{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -5.0f} };

#pragma endregion

	//=================
	// 映像演出用の変数
	//=================

#pragma region 映像演出用の変数

	// 映像演出の切り替え用フラグ(初期値はOFF)
	bool isVisualDirection = false;

	// 共通設定パラメータ //

	// アニメーション速度(倍率)
	float effectSpeed = 1.0f;

	// 演出用の単色カラー(RGBA)
	Vector4 effectColor = { 1.0f, 1.0f, 1.0f, 1.0f };

	// 演出用三角形全体のベースSRT(ImGui操作用)
	Transform effectBaseTransform{ {0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

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

// 開発用UIの処理
#ifdef USE_IMGUI

			// 一番外枠のウィンドウ「Settings」を開始する
			if (ImGui::Begin("Settings")) {

				//===========================================================
				// 【映像演出の切り替え】
				//===========================================================

				// チェックボックスを表示し、isVisualDirectionの値を直接書き換える
				ImGui::Checkbox("Visual Direction", &isVisualDirection);

				// 共通で表示するためのテクスチャパスの配列
				const char* texturePaths[] = {
					"resources/uvChecker.png",
					"resources/monsterBall.png"
				};

				//======映像演出がOFFの時=======//
				if (!isVisualDirection) {

					//=================================================================
					// 【1枚目の三角形の設定項目(TransformとDelete/Reset)】
					//=================================================================

					if (isAlive1) {

						// 「▼ Object1」ヘッダーを作成する
						if (ImGui::CollapsingHeader("Object##1", ImGuiTreeNodeFlags_DefaultOpen)) {

							// 1枚目用のSRT操作
							ImGui::DragFloat3("Translate##1", &transform1.translate.x, 0.01f);
							ImGui::DragFloat3("Rotate##1", &transform1.rotate.x, 0.01f);
							ImGui::DragFloat3("Scale##1", &transform1.scale.x, 0.01f);

							// 1枚目のDeleteボタン
							if (ImGui::Button("Delete##1")) {
								isAlive1 = false;
							}

							// 生存時のResetボタン
							ImGui::SameLine();
							if (ImGui::Button("Reset##1")) {
								transform1.scale = { 1.0f, 1.0f, 1.0f };
								transform1.rotate = { 0.0f, 0.0f, 0.0f };
								transform1.translate = { 0.0f, 0.0f, 0.0f };
							}
						}
					}
					else {
						// 消えている時のResetボタン
						ImGui::Text("Object1 is Deleted.");
						ImGui::SameLine();
						if (ImGui::Button("Reset Object1")) {
							isAlive1 = true;
							transform1.scale = { 1.0f, 1.0f, 1.0f };
							transform1.rotate = { 0.0f, 0.0f, 0.0f };
							transform1.translate = { 0.0f, 0.0f, 0.0f };
						}
						ImGui::Separator();
					}

					//=================================================================
					// 【2枚目の三角形の設定項目(TransformとDelete/Reset)】
					//=================================================================

					if (isAlive2) {

						// 「▼ Object2」ヘッダーを作成する
						if (ImGui::CollapsingHeader("Object##2", ImGuiTreeNodeFlags_DefaultOpen)) {

							// 2枚目用のSRT操作
							ImGui::DragFloat3("Translate##2", &transform2.translate.x, 0.01f);
							ImGui::DragFloat3("Rotate##2", &transform2.rotate.x, 0.01f);
							ImGui::DragFloat3("Scale##2", &transform2.scale.x, 0.01f);

							// 2枚目のDeleteボタン
							if (ImGui::Button("Delete##2")) {
								isAlive2 = false;
							}

							// 生存時のResetボタン
							ImGui::SameLine();
							if (ImGui::Button("Reset##2")) {
								transform2.scale = { 1.0f, 1.0f, 1.0f };
								transform2.rotate = { 0.0f, 0.0f, 0.0f };
								transform2.translate = { 0.0f, 0.0f, 0.0f };
							}
						}
					}
					else {
						// 消えている時のResetボタン
						ImGui::Text("Object2 is Deleted.");
						ImGui::SameLine();
						if (ImGui::Button("Reset Object2")) {
							isAlive2 = true;
							transform2.scale = { 1.0f, 1.0f, 1.0f };
							transform2.rotate = { 0.0f, 0.0f, 0.0f };
							transform2.translate = { 0.0f, 0.0f, 0.0f };
						}
						ImGui::Separator();
					}

					//=================================================================
					// 【共通マテリアル＆テクスチャの設定】
					//=================================================================

					if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {

						// 共通のマテリアルカラーデータを編集
						ImGuiColorEditFlags flags =
							ImGuiColorEditFlags_AlphaBar |
							ImGuiColorEditFlags_PickerHueBar |
							ImGuiColorEditFlags_DisplayRGB |
							ImGuiColorEditFlags_DisplayHex;

						ImGui::ColorPicker4("color", &materialData->x, flags);

						// インデックスが配列範囲(0 ~ 1)に収まるよう強制チェック
						if (textureIndex1 < 0 || textureIndex1 >= 2) {
							textureIndex1 = 0;
						}

						// 2枚目の三角形も範囲外なら0に戻す
						if (textureIndex2 < 0 || textureIndex2 >= 2) {
							textureIndex2 = 0;
						}

						// 共通テクスチャの切り替えコンボボックス
						if (ImGui::BeginCombo("Texture", texturePaths[textureIndex1])) {

							for (int i = 0; i < 2; i++) {

								bool isSelected = (textureIndex1 == i);

								if (ImGui::Selectable(texturePaths[i], isSelected)) {
									textureIndex1 = i;
									textureIndex2 = i;
								}

								if (isSelected) {
									ImGui::SetItemDefaultFocus();
								}
							}
							ImGui::EndCombo();
						}

						// マテリアル用の一発リセットボタン
						if (ImGui::Button("Reset Material & Texture")) {
							*materialData = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
							textureIndex1 = 0;
							textureIndex2 = 0;
						}
					}
				}
				else {

					//=================================================================
					// Visual DirectionがONの時のみ演出用の詳細UIを出す
					//=================================================================

					ImGui::Text("--- Visual Direction Settings ---");

					// 演出全体の速度変更スライダー
					ImGui::DragFloat("Effect Speed", &effectSpeed, 0.05f, 0.0f, 5.0f, "%.2f");
					ImGui::Separator();

					// 5モード対応ブレンドモードコンボボックス
					const char* blendNames[] = {
						"None",
						"Alpha",
						"Add",
						"Subtract",
						"Screen"
					};

					if (ImGui::BeginCombo("Blend Mode", blendNames[currentBlendIndex])) {
						for (int i = 0; i < BlendMode_Count; i++) {
							bool isSelected = (currentBlendIndex == i);
							if (ImGui::Selectable(blendNames[i], isSelected)) {
								currentBlendIndex = i;
							}
							if (isSelected) {
								ImGui::SetItemDefaultFocus();
							}
						}
						ImGui::EndCombo();
					}

					ImGui::Separator();

					// イージングコンボボックス(初期値Linear)
					const char* easingNames[] = {
						// 0: イージングなし
						"Linear(No Easing)",
						// 1
						"Ease In",
						// 2
						"Ease Out",
						// 3
						"Ease In Out",
						// 4
						"Ease In Back",
						// 5
						"Ease Out Back",
						// 6
						"Ease In Quart",
						// 7
						"Ease Out Quart",
						// 8
						"Ease In Out Quart"
					};

					if (ImGui::BeginCombo("Easing Type", easingNames[currentEasingType])) {
						for (int i = 0; i < 9; i++) {
							bool isSelected = (currentEasingType == i);
							if (ImGui::Selectable(easingNames[i], isSelected)) {
								currentEasingType = i;
							}
							if (isSelected) {
								ImGui::SetItemDefaultFocus();
							}
						}
						ImGui::EndCombo();
					}

					ImGui::Separator();

					// 演出オブジェクト全体のベースSRT操作UI
					if (ImGui::CollapsingHeader("Effect Base SRT", ImGuiTreeNodeFlags_DefaultOpen)) {
						ImGui::DragFloat3("Base Scale", &effectBaseTransform.scale.x, 0.01f);
						ImGui::DragFloat3("Base Rotate", &effectBaseTransform.rotate.x, 0.01f);
						ImGui::DragFloat3("Base Translate", &effectBaseTransform.translate.x, 0.01f);
					}
					ImGui::Separator();

					// 演出専用の大型カラーピッカー
					ImGui::Text("Effect Material Color");
					ImGuiColorEditFlags effectFlags =
						ImGuiColorEditFlags_AlphaBar |
						ImGuiColorEditFlags_PickerHueBar |
						ImGuiColorEditFlags_DisplayRGB |
						ImGuiColorEditFlags_DisplayHex;

					ImGui::ColorPicker4("Effect Color", &effectColor.x, effectFlags);
				}

				// 一番外枠のウィンドウ「Settings」を終了する
				ImGui::End();
			}

#endif

			Matrix4x4 cameraMatrix = MathUtils::MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);

			Matrix4x4 viewMatrix = MathUtils::Inverse(cameraMatrix);

			// 透視投影行列の作成
			Matrix4x4 projectionMatrix = MathUtils::MakePerspectiveFovMatrix(0.45f, static_cast<float>(kClientWidth) / static_cast<float>(kClientHeight), 0.1f, 100.0f);

			//======================================
			// 映像演出の自動SRT計算&データ書き込み
			//======================================

			if (isVisualDirection) {

				// タイマーの進み幅(effectSpeedで制御)
				effectTimer += 0.02f * effectSpeed;

				// ImGuiからのベースSRT(全体の拡大率・回転・位置)からベース行列を作成
				Matrix4x4 baseMatrix = MathUtils::MakeAffineMatrix(
					effectBaseTransform.scale,
					effectBaseTransform.rotate,
					effectBaseTransform.translate
				);

				for (int i = 0; i < kMaxEffectObjects; i++) {
					Transform localTransform;

					// 三角形1つ1つの大きさ
					localTransform.scale = { 1.0f, 1.0f, 1.0f };

					// 渦の一部として自然に見えるよう、三角形の向きを進行方向に合わせる(接線方向を向かせる)

					// 各オブジェクトの渦の中での位置(角度のズレ)
					float spiralAngleOffset = static_cast<float>(i) * 0.2f;

					// 現在の合計角度
					float currentAngle = effectTimer * 2.0f + spiralAngleOffset;

					localTransform.rotate.z = currentAngle;
					localTransform.rotate.y = 0.0f;
					localTransform.rotate.x = 0.0f;

					// 渦巻きの座標計算 //

					// まずは時間経過による均等な進捗(0.0f ~ 1.0f)を計算する
					float rawProgress = std::fmod((effectTimer * 0.15f) + (static_cast<float>(i) / kMaxEffectObjects), 1.0f);

					// 0番なら等速、それ以外なら選択されたイージング関数を動的に適用する
					float progress = 0.0f;

					switch (currentEasingType) {

						// 0: イージングなし(等速直線運動)
					case 0:
						progress = rawProgress;
						break;

						//---------------------------------------------------------
						// 1 ~ 3: Sine(正弦波)系イージング(比較的滑らかな変化)
						//---------------------------------------------------------

					case 1: // Ease In: ゆっくり始まり、だんだん加速する
						progress = Easing::EaseIn(rawProgress);
						break;

					case 2: // Ease Out: 勢いよく始まり、だんだん減速する
						progress = Easing::EaseOut(rawProgress);
						break;

					case 3: // Ease In Out: ゆっくり始まり、途中で加速し、ゆっくり終わる
						progress = Easing::EaseInOut(rawProgress);
						break;

						//---------------------------------------------------------
						// 4 ~ 5: Back系イージング(一度逆方向にタメたり、行き過ぎたりする)
						//---------------------------------------------------------

					case 4: // Ease In Back: 一度後ろに少し下がって(タメて)から加速する
						progress = Easing::EaseInBack(rawProgress);
						break;

					case 5: // Ease Out Back: 勢いよく飛び出し、目標を少し行き過ぎてから戻る
						progress = Easing::EaseOutBack(rawProgress);
						break;

						//---------------------------------------------------------
						// 6 ~ 8: Quart(4乗)系イージング(Sineよりも非常に強い緩急)
						//---------------------------------------------------------

					case 6: // Ease In Quart: 始まりが非常に遅く、後半で急激に加速する
						progress = Easing::EaseInQuart(rawProgress);
						break;

					case 7: // Ease Out Quart: 最初は爆発的に速く、後半で急激にブレーキがかかる
						progress = Easing::EaseOutQuart(rawProgress);
						break;

					case 8: // Ease In Out Quart: 加減速のメリハリが最も強い
						progress = Easing::EaseInOutQuart(rawProgress);
						break;

						// 例外処理(安全対策)
					default:
						progress = rawProgress;
						break;
					}

					// 「緩急のついた progress」を使ってZ軸や半径を計算する //

					// 少し奥の開始位置を深めにするとよりタメが活きます
					float startZ = 50.0f;

					// カメラ(Z = -5.0)を完全に突き抜ける位置
					float endZ = -5.0f;
					localTransform.translate.z = startZ + (endZ - startZ) * progress;

					// 手前に来たときは画面を覆い尽くすぐらい広げる
					float maxRadius = 12.0f;
					float radius = progress * maxRadius;

					// 円周上の位置(X座標, Y座標)を計算
					localTransform.translate.x = std::cos(currentAngle) * radius;
					localTransform.translate.y = std::sin(currentAngle) * radius;

					// 行列の合成
					Matrix4x4 localMatrix = MathUtils::MakeAffineMatrix(
						localTransform.scale,
						localTransform.rotate,
						localTransform.translate
					);

					// 個別行列にベースのSRT行列を乗算することで、ImGuiのSRT操作を有効化
					Matrix4x4 worldMatrixEff = MathUtils::Multiply(localMatrix, baseMatrix);
					Matrix4x4 wvpMatrixEff = MathUtils::Multiply(worldMatrixEff, MathUtils::Multiply(viewMatrix, projectionMatrix));

					*effectWvpData[i] = wvpMatrixEff;
				}
			}

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

			//==========================================
			// 三角形(3D)の描画設定
			//==========================================

			// 描画先のRTVとDSVを設定する
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);

			// 青っぽい色。RGBAの順番で指定する
			float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };

			// 指定した色で画面全体をクリアする
			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);

			// 指定した深度で画面全体をクリアする
			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

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

			// SRVの切り替え!初期値はuvChecker(インデックス1)
			D3D12_GPU_DESCRIPTOR_HANDLE currentSrvHandleGPU = textureSrvHandleGPU;

			// CBVを設定する(マテリアルCBufferの場所を設定)。rootParameters[0]へ設定
			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());

			// 映像演出がOFFの時のみ描画する2枚の三角形
			if (!isVisualDirection) {

				//==========================================
				// 1枚目の三角形の描画処理
				//==========================================

				if (isAlive1 == true) {

					// transform1(ImGuiで動く値)からアフィン変換行列を作成
					Matrix4x4 worldMatrix1 = MathUtils::MakeAffineMatrix(transform1.scale, transform1.rotate, transform1.translate);

					// WVP行列の計算
					Matrix4x4 wvpMatrix1 = MathUtils::Multiply(worldMatrix1, MathUtils::Multiply(viewMatrix, projectionMatrix));

					// 1番用の定数バッファへWVP行列データを書き込む
					*wvpData1 = wvpMatrix1;

					// 定数バッファのバインド(レジスタ1番)
					commandList->SetGraphicsRootConstantBufferView(1, wvpResource1->GetGPUVirtualAddress());

					// 共通コンボボックスの選択に応じてSRVハンドルを切り替える

					// 0番:uvChecker
					D3D12_GPU_DESCRIPTOR_HANDLE srvHandle1 = textureSrvHandleGPU;

					if (textureIndex1 == 1) {
						// 1番:monsterBall
						srvHandle1 = textureSrvHandleGPU2;
					}

					// SRVのDescriptorTableを設定(rootParameters[2]の場所)
					commandList->SetGraphicsRootDescriptorTable(2, srvHandle1);

					// 描画コマンド(1枚目の三角形である頂点インデックス0から3つを描画)
					commandList->DrawInstanced(3, 1, 0, 0);
				}

				//==========================================
				// 2枚目の三角形の描画処理
				//==========================================

				if (isAlive2 == true) {

					// transform2(ImGuiで動く値)からアフィン変換行列を作成
					Matrix4x4 worldMatrix2 = MathUtils::MakeAffineMatrix(transform2.scale, transform2.rotate, transform2.translate);

					// WVP行列の計算
					Matrix4x4 wvpMatrix2 = MathUtils::Multiply(worldMatrix2, MathUtils::Multiply(viewMatrix, projectionMatrix));

					// 2番用の定数バッファへWVP行列データを書き込む
					*wvpData2 = wvpMatrix2;

					// 定数バッファのバインド(レジスタ1番)
					commandList->SetGraphicsRootConstantBufferView(1, wvpResource2->GetGPUVirtualAddress());

					// デフォルトを「textureSrvHandleGPU(uvChecker)」に統一し、インデックスが1のときだけ(monsterBall)に切り替える

					// 0番:uvChecker(初期値)
					D3D12_GPU_DESCRIPTOR_HANDLE srvHandle2 = textureSrvHandleGPU;

					if (textureIndex2 == 1) {
						// 1番:monsterBall
						srvHandle2 = textureSrvHandleGPU2;
					}

					// SRVのDescriptorTableを設定(rootParameters[2]の場所)
					commandList->SetGraphicsRootDescriptorTable(2, srvHandle2);

					// 描画コマンド
					commandList->DrawInstanced(3, 1, 3, 0);
				}
			}

			//=================================================================
			// 「映像演出」の複数の三角形の描画
			//=================================================================

			if (isVisualDirection) {

				// ImGuiで選択されているブレンドモードのPSOをバインドする
				commandList->SetPipelineState(effectPipelineStates[currentBlendIndex]);

				// マテリアルカラーをImGuiで選んだ色で毎フレーム上書きする
				if (materialData != nullptr) {
					*materialData = effectColor;
				}
				commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());

				// ルートシグネチャのエラーを防ぐため、安全にテクスチャをバインドしておく
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);

				for (int i = 0; i < kMaxEffectObjects; i++) {
					// 各オブジェクト専用の定数バッファをバインド
					commandList->SetGraphicsRootConstantBufferView(1, effectWvpResources[i]->GetGPUVirtualAddress());

					// 描画!!(頂点バッファの0番から3つの頂点を使って三角形を描画)
					commandList->DrawInstanced(3, 1, 0, 0);
				}
			}

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

	if (effectPipelineStates[currentBlendIndex]) {
		effectPipelineStates[currentBlendIndex]->Release();
	}

	if (effectPixelShaderBlob) { effectPixelShaderBlob->Release(); }

	rootSignature->Release();

	for (int i = 0; i < kMaxEffectObjects; i++) {
		if (effectWvpResources[i]) {
			effectWvpResources[i]->Release();
		}
	}

	if (signatureBlob) {
		signatureBlob->Release();
	}

	if (errorBlob) {
		errorBlob->Release();
	}

	pixelShaderBlob->Release();
	vertexShaderBlob->Release();

	// バッファ・マテリアルリソースの解放
	if (wvpResource1) {
		wvpResource1->Release();
	}

	if (wvpResource2) {
		wvpResource2->Release();
	}

	materialResource->Release();
	vertexResource->Release();

	// ディスクリプタヒープの解放
	srvDescriptorHeap->Release();
	rtvDescriptorHeap->Release();

	// スワップチェーン関連の解放
	if (swapChainResources[0]) {
		swapChainResources[0]->Release();
	}

	if (swapChainResources[1]) {
		swapChainResources[1]->Release();
	}

	swapChain->Release();

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