#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <fstream>
#include <sstream>
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
#include <wrl.h>
#include "Audio.h"
#include "Input.h"
#include "DebugCamera.h"
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
// 関数群・構造体群
//=======================

#pragma region 関数群・構造体群

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
	Vector3 normal;
};

// マテリアル
struct Material {
	Vector4 color;
	int32_t lightingMode;
	float padding[3];
	Matrix4x4 uvTransform;
};

// UVTransform用の変数
Transform uvTransformSprite{
	{1.0f, 1.0f, 1.0f},
	{0.0f, 0.0f, 0.0f},
	{0.0f, 0.0f, 0.0f},
};

// TransformationMatrix
struct TransformationMatrix {
	Matrix4x4 wvp;
	Matrix4x4 world;
};

// 平行光源
struct DirectionalLight {
	// ライトの色
	Vector4 color;
	// ライトの向き
	Vector3 direction;
	// 輝度(明るさ)
	float intensity;
};

// マテリアルデータ
struct MaterialData {
	std::string textureFilePath;
	// このマテリアルで描画する頂点数
	uint32_t vertexCount = 0;
	// 頂点バッファ内の開始位置
	uint32_t vertexStartIndex = 0;
};

// モデルデータ
struct ModelData {
	std::vector<VertexData> vertices;
	// 単一マテリアル用
	MaterialData material;
	// 複数マテリアル対応用
	std::vector<MaterialData> materials;
};

struct D3DResourceLeakChecker {

	~D3DResourceLeakChecker() {

		// リソースリークチェック
		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		}
	}
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
Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(const Microsoft::WRL::ComPtr<ID3D12Device>& device, size_t sizeInBytes) {

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

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
	const Microsoft::WRL::ComPtr<ID3D12Device>& device,
	D3D12_DESCRIPTOR_HEAP_TYPE heapType,
	UINT numDescriptors,
	bool shaderVisible) {

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

//====================
// CompileShader関数
//====================

#pragma region CompileShader関数

Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
	const std::wstring& filePath,
	const wchar_t* profile,
	IDxcUtils* dxcUtils,
	IDxcCompiler3* dxcCompiler,
	IDxcIncludeHandler* includeHandler) {

	// これからシェーダーをコンパイルする旨をログに出力する
	Log(ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));

	// hlslファイルを読む
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
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
	Microsoft::WRL::ComPtr<IDxcResult> shaderResult = nullptr;
	hr = dxcCompiler->Compile(&shaderSourceBuffer, arguments, _countof(arguments), includeHandler, IID_PPV_ARGS(&shaderResult));

	// コンパイルエラーではなくdxcが起動できないなどの根本的なエラーが発生した場合は止める
	assert(SUCCEEDED(hr));

	// 警告・エラーが出ていたらログに出力して止める
	Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError = nullptr; shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);

	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		Log(shaderError->GetStringPointer());

		// 警告・エラーが出ている場合は止める
		assert(false);
	}

	// コンパイル結果から実行用のバイナリ部分を取得する
	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr; shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));

	// 成功したログを出力する
	Log(ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));

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

Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(
	const Microsoft::WRL::ComPtr<ID3D12Device>& device,
	const DirectX::TexMetadata& metaData) {

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
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));

	return resource;
}

#pragma endregion

//===========================================
// UploadTextureData関数(データを転送する関数)
//===========================================

#pragma region UploadTextureData関数(データを転送する関数)

[[nodiscard]] Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(
	const Microsoft::WRL::ComPtr<ID3D12Resource>& texture,
	const DirectX::ScratchImage& mipImages,
	const Microsoft::WRL::ComPtr<ID3D12Device>& device,
	const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList) {

	// 中間リソース
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;

	// PrepareUploadを利用して、読み込んだデータからDirectX12用のSubresourceの配列を作成する
	DirectX::PrepareUpload(device.Get(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);

	// Subresourceの数を基に、コピー元となるintermediateResourceに必要なサイズを計算する
	uint64_t intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, UINT(subresources.size()));

	// 計算したサイズでintermediateResourceを作成する
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = CreateBufferResource(device.Get(), intermediateSize);

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

#pragma endregion

//================================
// CreateDepthStencilTexture関数
//================================

#pragma region DepthStencilTexture関数

Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTexture(
	const Microsoft::WRL::ComPtr<ID3D12Device>& device,
	int32_t width,
	int32_t height) {

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
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;

	HRESULT hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClearValue, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));

	return resource;
}

#pragma endregion

//==============================
// DescriptorCPUHandle取得関数
//==============================

#pragma region DescriptorCPUHandle取得関数

D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(
	const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
	uint32_t descriptorSize,
	uint32_t index) {

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

D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(
	const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap,
	uint32_t descriptorSize,
	uint32_t index) {

	// ディスクリプタヒープの先頭のハンドルを取得する
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();

	// index分だけハンドルを進める
	handleGPU.ptr += (descriptorSize * index);

	return handleGPU;
}

#pragma endregion

//====================================================
// LoadMaterialTemplateFile関数(mtlファイルを読む関数)
//====================================================

#pragma region LoadMaterialTemplateFile関数(mtlファイルを読む関数)

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {

	// ①必要な変数の宣言 //

	// 構築するMaterialData
	MaterialData materialData;

	// ファイルから読んだ1行を格納するもの
	std::string line;

	// ②ファイルを開く //
	std::ifstream file(directoryPath + "/" + filename);

	// とりあえず開けなかったら止める
	assert(file.is_open());

	// ③実際にファイルを読み、MaterialDataを構築していく //

	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		// identifierに応じた処理
		if (identifier == "map_Kd") {
			std::string textureFilename;
			s >> textureFilename;

			// 連結してファイルパスにする
			materialData.textureFilePath = directoryPath + "/" + textureFilename;
		}
	}

	// ④MaterialDataを返す
	return materialData;
}

#pragma endregion

//=======================================
// LoadObjFile関数(Objファイルを読む関数)
//=======================================

#pragma region LoadObjFile関数(Objファイルを読む関数)

ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename) {

	// ①中で必要となる変数の宣言 //

	// 構築するModelData
	ModelData modelData;

	// 位置
	std::vector<Vector4> positions;

	// 法線
	std::vector<Vector3> normals;

	// テクスチャ座標
	std::vector<Vector2> texcoords;

	// ファイルから読み込んだ1行を格納するもの
	std::string line;

	// マテリアル定義(.mtl)の情報を一時保持するマップ
	std::unordered_map<std::string, std::string> materialMap;

	// ②ファイルを開く //

	std::ifstream file(directoryPath + "/" + filename);

	// とりあえず開けなかったら止める
	assert(file.is_open());

	// ③実際にファイルを読み、ModelDataを構築していく //

	while (std::getline(file, line)) {

		std::string identifier;
		std::istringstream s(line);

		// 先頭の識別子を読む
		s >> identifier;

		// identifierに応じた処理
		if (identifier == "v") {
			Vector4 position;
			s >> position.x >> position.y >> position.z;
			position.w = 1.0f;

			// 位置のX座標を反転させる
			position.x *= -1.0f;
			positions.push_back(position);
		} else if (identifier == "vt") {
			Vector2 texcoord;
			s >> texcoord.x >> texcoord.y;

			// すべてのモデルのUVのY(V軸)を読み込み時点で反転させ、DirectXの左上原点に統一する
			texcoord.y = 1.0f - texcoord.y;
			texcoords.push_back(texcoord);
		} else if (identifier == "vn") {
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;

			// 法線のX座標を反転させる
			normal.x *= -1.0f;
			normals.push_back(normal);

			// マテリアルの切り替え
		} else if (identifier == "usemtl") {

			std::string useMaterialName;
			s >> useMaterialName;

			// 新しいマテリアルグループを開始
			MaterialData matData;

			// デフォルトのテクスチャ名推測、またはmtl解析値
			matData.textureFilePath = directoryPath + "/" + useMaterialName + ".png";
			matData.vertexStartIndex = static_cast<uint32_t>(modelData.vertices.size());
			matData.vertexCount = 0;

			modelData.materials.push_back(matData);

		} else if (identifier == "f") {

			VertexData triangle[3];

			// 面は三角形限定(その他は未対応)
			for (int32_t faceVertex = 0; faceVertex < 3; faceVertex++) {
				std::string vertexDefinition;
				s >> vertexDefinition;

				std::istringstream v(vertexDefinition);
				std::string indexStr;

				// 各インデックスの初期値を 0(未設定)とする
				uint32_t positionIndex = 0;
				uint32_t texcoordIndex = 0;
				uint32_t normalIndex = 0;

				// 位置(Position)
				if (std::getline(v, indexStr, '/') && !indexStr.empty()) {
					positionIndex = std::stoi(indexStr);
				}

				// UV(Texcoord)
				if (std::getline(v, indexStr, '/') && !indexStr.empty()) {
					texcoordIndex = std::stoi(indexStr);
				}

				// 法線(Normal)
				if (std::getline(v, indexStr, '/') && !indexStr.empty()) {
					normalIndex = std::stoi(indexStr);
				}

				// インデックスをもとに要素を取得(未設定の場合はデフォルト値)
				Vector4 position = (positionIndex > 0 && positionIndex <= positions.size())
					? positions[positionIndex - 1] : Vector4{ 0.0f, 0.0f, 0.0f, 1.0f };

				// UVがない場合は {0, 0} にする
				Vector2 texcoord = (texcoordIndex > 0 && texcoordIndex <= texcoords.size())
					? texcoords[texcoordIndex - 1] : Vector2{ 0.0f, 0.0f };

				Vector3 normal = (normalIndex > 0 && normalIndex <= normals.size())
					? normals[normalIndex - 1] : Vector3{ 0.0f, 1.0f, 0.0f };

				triangle[faceVertex] = { position, texcoord, normal };
			}

			// 頂点を逆順で登録(時計回り/反時計回りの合わせ)
			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[0]);

			if (!modelData.materials.empty()) {
				modelData.materials.back().vertexCount += 3;
			}

		} else if (identifier == "mtllib") {

			// materialTemplateLibraryファイルの名前を取得する
			std::string materialFilename;
			s >> materialFilename;

			// 基本的にobjファイルと同一階層にmtlを存在させるため、ディレクトリ名とファイル名を渡す
			modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
		}
	}

	// マテリアル(usemtl)が1つも指定されていなかった場合のバックアップ処理
	if (modelData.materials.empty()) {
		MaterialData defaultMat;
		defaultMat.textureFilePath = modelData.material.textureFilePath;
		defaultMat.vertexStartIndex = 0;
		defaultMat.vertexCount = static_cast<uint32_t>(modelData.vertices.size());
		modelData.materials.push_back(defaultMat);
	}

	// ④modelDataを返す //
	return modelData;
}

#pragma endregion

//===============
// main関数
//===============

#pragma region main関数

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

	D3DResourceLeakChecker leakCheck;
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory;
	Microsoft::WRL::ComPtr<ID3D12Device> device;
	Microsoft::WRL::ComPtr<IXAudio2> xAudio2;
	IXAudio2MasteringVoice* masterVoice = nullptr;
	IXAudio2SourceVoice* pSourceVoice = nullptr;

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
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));

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
			Log(std::format("Feature Level:{}\n", featureLevelStrings[i]));
			break;
		}
	}

	// デバイスが生成できなかった場合は起動できないため、assertで止める
	assert(device.Get() != nullptr);

	// 初期化完了のログを出力する
	Log("Conplete create D3D12 Device!!!\n");

#pragma endregion

	//======================================
	// 入力管理クラスのインスタンス生成と初期化
	//======================================

	// Inputクラスのポインタを変数として宣言する
	Input* input = new Input();

	// 作成したインスタンスの初期化関数を呼び出す
	input->Initialize(wc.hInstance, hwnd);

	//======================
	// XAudio2の初期化
	//======================

#pragma region XAudio2の初期化

	// XAudio2エンジンのインスタンスを生成
	hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);

	// マスターボイスの作成
	masterVoice = nullptr;

	hr = xAudio2->CreateMasteringVoice(&masterVoice);

#pragma endregion

	//=======================
	// エラー及び警告での停止
	//=======================

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

	//=======================
	// コマンドキューの生成
	//=======================

#pragma region コマンドキューの生成

	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
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
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

	// コマンドアロケータの生成が上手くいかなかったので、起動できない
	assert(SUCCEEDED(hr));

	// コマンドリストを生成する
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));

	// コマンドリストの生成が上手くいかなかったので、起動できない
	assert(SUCCEEDED(hr));

#pragma endregion

	//======================
	// スワップチェーンの生成
	//======================

#pragma region スワップチェーンの生成

	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain = nullptr;
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
	hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf()));
	assert(SUCCEEDED(hr));

#pragma endregion

	//==========================
	// RTVDescriptorHeapの生成
	//==========================

#pragma region RTVDescriptorHeapの生成

	// RTV用のヒープでディスクリプタヒープの数は2。RTVはShader内で触るものではないので、shaderVisibleはfalse
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap = CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);

	// スワップチェーンからバッファを一時的に取得
	Microsoft::WRL::ComPtr<ID3D12Resource> tempBuffer0 = nullptr;
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&tempBuffer0));
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
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	//==============================
	// DepthStencilResourceの生成
	//==============================

#pragma region DepthStencilResourceの生成

	// ウィンドウと同じサイズで生成
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource = CreateDepthStencilTexture(device.Get(), kClientWidth, kClientHeight);

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
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap = CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

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
	device->CreateRenderTargetView(swapChainResources[0].Get(), &rtvDesc, rtvHandles[0]);

	// 2つ目のディスクリプタハンドルを得る
	rtvHandles[1].ptr = rtvHandles[0].ptr + descriptorSizeRTV;

	// 2つ目のRTVを作る
	device->CreateRenderTargetView(swapChainResources[1].Get(), &rtvDesc, rtvHandles[1]);

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
	Microsoft::WRL::ComPtr<ID3D12Fence> fence = nullptr;
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
	D3D12_ROOT_PARAMETER rootParameters[4] = {};

	// CBVを使う
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;

	// PixelShaderを使う
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// レジスタ番号0とバインドする
	rootParameters[0].Descriptor.ShaderRegister = 0;

	rootParameters[0].Descriptor.RegisterSpace = 0;

	// CBVを使う
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;

	// VertexShaderで使う
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// レジスタ番号1を使う
	rootParameters[1].Descriptor.ShaderRegister = 1;

	// DescriptorTableを使う
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;

	// PixelShaderで使う
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// Tableの中身の配列を指定
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;

	// Tableで利用する数
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	// CBVを使う
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;

	// PixelShaderで使う
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// レジスタ番号3を使う
	rootParameters[3].Descriptor.ShaderRegister = 3;

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
	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);

	if (FAILED(hr)) {
		if (errorBlob) {
			Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
			assert(false);
		}
	}

	// バイナリを元に生成
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));

#pragma endregion

	//====================
	// InputLayoutの設定
	//====================

#pragma region InputLayoutの設定

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};

	// 頂点の位置。シェーダー側のSemanticはPOSITION
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TexCoord";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

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

	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(L"Object3d.VS.hlsl", L"vs_6_0", dxcUtils, dxcCompiler, dxcIncludeHandler);
	assert(vertexShaderBlob != nullptr);

	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(L"Object3d.PS.hlsl", L"ps_6_0", dxcUtils, dxcCompiler, dxcIncludeHandler);
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
	graphicsPilelineStateDesc.pRootSignature = rootSignature.Get();

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
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState = nullptr;
	hr = device->CreateGraphicsPipelineState(&graphicsPilelineStateDesc, IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));

#pragma endregion

	//===================================
	// オーディオシステムの初期化と読み込み
	//===================================

	Audio* audioManager = Audio::GetInstance();

	// XAudio2の初期化
	audioManager->Initialize();

	//======================
	// 音声データの読み込み
	//======================

	Audio::SoundData soundData1 = audioManager->SoundLoadWave("resources/Alarm01.wav");

	//=======================
	// VertexResourceの生成
	//=======================

#pragma region VertexResourceの生成

//====================================
// 「plane.obj」モデルの読み込み
//====================================

	ModelData planeModelData = LoadObjFile("resources", "plane.obj");

	// 「plane.obj」モデルの頂点リソースの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> planeModelVertexResource = CreateBufferResource(device.Get(), sizeof(VertexData) * planeModelData.vertices.size());

	//=========================
	// 「teapot.obj」の読み込み
	//=========================

	ModelData teapotModelData = LoadObjFile("resources", "teapot.obj");

	// 「teapot.obj」モデルの頂点リソースの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> teapotModelVertexResource = CreateBufferResource(device.Get(), sizeof(VertexData) * teapotModelData.vertices.size());

	//=========================
	// 「bunny.obj」の読み込み
	//=========================

	ModelData bunnyModelData = LoadObjFile("resources", "bunny.obj");

	// 「bunny.obj」モデルの頂点リソースの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> bunnyModelVertexResource = CreateBufferResource(device.Get(), sizeof(VertexData) * bunnyModelData.vertices.size());

	//=============================
	// 「MultiMesh.obj」の読み込み
	//=============================

	ModelData multiMeshModelData = LoadObjFile("resources", "multiMesh.obj");

	// 「multiMesh.obj」モデルの頂点リソースの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> multiMeshModelVertexResource = CreateBufferResource(device.Get(), sizeof(VertexData) * multiMeshModelData.vertices.size());

	//================================
	// 「MultiMaterial.obj」の読み込み
	//================================

	ModelData multiMaterialModelData = LoadObjFile("resources", "multiMaterial.obj");

	// 「multiMaterial.obj」モデルの頂点リソースの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> multiMaterialModelVertexResource = CreateBufferResource(device.Get(), sizeof(VertexData) * multiMaterialModelData.vertices.size());

	//================================
	// 「suzanne.obj」の読み込み
	//================================

	ModelData suzanneModelData = LoadObjFile("resources", "suzanne.obj");

	// 「suzanne.obj」モデルの頂点リソースの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> suzanneModelVertexResource = CreateBufferResource(device.Get(), sizeof(VertexData) * suzanneModelData.vertices.size());

	//====================
	// Sphere
	//====================

	// 球体の分割数に合わせたサイズ
	const uint32_t kSubdivision = 16;

	// インデックス用に重複なしの頂点数(289個)にする
	uint32_t vertexCount = (kSubdivision + 1) * (kSubdivision + 1);
	size_t sizeInBytes = sizeof(VertexData) * vertexCount;

	// 球の頂点リソースの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> sphereVertexResource = CreateBufferResource(device.Get(), sizeInBytes);

	//=======================
	// Sprite
	//=======================

	// Sprite用の頂点リソースを作成する
	Microsoft::WRL::ComPtr<ID3D12Resource> spriteVertexResource = CreateBufferResource(device.Get(), sizeof(VertexData) * 6);

#pragma endregion

	//===================================
	// PlaneModelMaterialResourceの生成
	//===================================

#pragma region PlaneModelMaterialResourceの生成

	// マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> planeModelMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));

	// マテリアルにデータを書き込む
	Material* planeModelMaterialData = nullptr;

	// 書き込むためのアドレスを取得する
	planeModelMaterialResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&planeModelMaterialData));

	// 構造体の各メンバにデータを代入する

	// 白色にする
	planeModelMaterialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Lightingを有効にする
	planeModelMaterialData->lightingMode = 0;
	planeModelMaterialData->uvTransform = MathUtils::MakeIdentity4x4();

#pragma endregion

	//===================================
	// TeapotModelMaterialResourceの生成
	//===================================

#pragma region TeapotModelMaterialResourceの生成

	// マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> teapotModelMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));

	// マテリアルにデータを書き込む
	Material* teapotModelMaterialData = nullptr;

	// 書き込むためのアドレスを取得する
	teapotModelMaterialResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&teapotModelMaterialData));

	// 構造体の各メンバにデータを代入する

	// 白色にする
	teapotModelMaterialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Lightingを有効にする
	teapotModelMaterialData->lightingMode = 0;
	teapotModelMaterialData->uvTransform = MathUtils::MakeIdentity4x4();

#pragma endregion

	//===================================
	// BunnyModelMaterialResourceの生成
	//===================================

#pragma region BunnyModelMaterialResourceの生成

	//===========================
	// 「bunny.obj」のマテリアル
	//===========================

	// マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> bunnyModelMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));

	// マテリアルにデータを書き込む
	Material* bunnyModelMaterialData = nullptr;

	// 書き込むためのアドレスを取得する
	bunnyModelMaterialResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&bunnyModelMaterialData));

	// 構造体の各メンバにデータを代入する

	// 白色にする
	bunnyModelMaterialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Lightingを有効にする
	bunnyModelMaterialData->lightingMode = 0;
	bunnyModelMaterialData->uvTransform = MathUtils::MakeIdentity4x4();

#pragma endregion

	//=======================================
	// MultiMeshModelMaterialResourceの生成
	//=======================================

#pragma region MultiMeshModelMaterialResourceの生成

	//==============================
	// 「multiMesh.obj」のマテリアル
	//==============================

	// マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> multiMeshModelMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));

	// マテリアルにデータを書き込む
	Material* multiMeshModelMaterialData = nullptr;

	// 書き込むためのアドレスを取得する
	multiMeshModelMaterialResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&multiMeshModelMaterialData));

	// 構造体の各メンバにデータを代入する

	// 白色にする
	multiMeshModelMaterialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Lightingを有効にする
	multiMeshModelMaterialData->lightingMode = 0;
	multiMeshModelMaterialData->uvTransform = MathUtils::MakeIdentity4x4();

#pragma endregion

	//===========================================
	// MultiMaterialModelMaterialResourceの生成
	//===========================================

#pragma region MultiMaterialModelMaterialResourceの生成

	//==================================
	// 「multiMaterial.obj」のマテリアル
	//==================================

	// マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> multiMaterialModelMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));

	// マテリアルにデータを書き込む
	Material* multiMaterialModelMaterialData = nullptr;

	// 書き込むためのアドレスを取得する
	multiMaterialModelMaterialResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&multiMaterialModelMaterialData));

	// 構造体の各メンバにデータを代入する

	// 白色にする
	multiMaterialModelMaterialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Lightingを有効にする
	multiMaterialModelMaterialData->lightingMode = 0;
	multiMaterialModelMaterialData->uvTransform = MathUtils::MakeIdentity4x4();

#pragma endregion

	//===========================================
	// suzanneModelMaterialResourceの生成
	//===========================================

#pragma region suzanneModelMaterialResourceの生成

	//==================================
	// 「suzanne.obj」のマテリアル
	//==================================

	// マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> suzanneModelMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));

	// マテリアルにデータを書き込む
	Material* suzanneModelMaterialData = nullptr;

	// 書き込むためのアドレスを取得する
	suzanneModelMaterialResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&suzanneModelMaterialData));

	// 構造体の各メンバにデータを代入する

	// 白色にする
	suzanneModelMaterialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Lightingを有効にする
	suzanneModelMaterialData->lightingMode = 0;
	suzanneModelMaterialData->uvTransform = MathUtils::MakeIdentity4x4();

#pragma endregion

	//================================
	// SphereMaterialResourceの生成
	//================================

#pragma region SphereMaterialResourceの生成

	Microsoft::WRL::ComPtr<ID3D12Resource> sphereMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));
	Material* sphereMaterialData = nullptr;
	sphereMaterialResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&sphereMaterialData));

	sphereMaterialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// 初期値: None
	sphereMaterialData->lightingMode = 0;
	sphereMaterialData->uvTransform = MathUtils::MakeIdentity4x4();

#pragma endregion

	//================================
	// SpriteMaterialResourceの生成
	//================================

#pragma region SpriteMaterialResourceの生成

	// Sprite用のMaterialResourceを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> spriteMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));

	// MaterialSpriteDataにデータを書き込む
	Material* spriteMaterialData = nullptr;

	// 書き込むためのアドレスを取得する
	spriteMaterialResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&spriteMaterialData));

	// 構造体の各メンバにデータを代入する

	// 白色にする
	spriteMaterialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// SpriteはLightingしないので0にする
	spriteMaterialData->lightingMode = 0;

	spriteMaterialData->uvTransform = MathUtils::MakeIdentity4x4();

#pragma endregion

	//=================================
	// DirectionalLightResourceの生成
	//=================================

#pragma region DirectionalLightResourceの生成

	// DirectionalLight用のResourceを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource = CreateBufferResource(device.Get(), sizeof(DirectionalLight));

	// DirectionalLightResourceにデータを書き込む
	DirectionalLight* directionalLightData = nullptr;

	// 書き込むためのアドレスを取得する
	directionalLightResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));

	// 構造体の各メンバにデータを代入する
	directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	directionalLightData->direction = { 0.0f, -1.0f, 0.0f };

	// 正規化する
	directionalLightData->direction = MathUtils::Normalize(directionalLightData->direction);
	directionalLightData->intensity = 1.0f;

#pragma endregion

	//============================
	// SphereIndexResourceの生成
	//============================

#pragma region SphereIndexResourceの生成

	// 球用のインデックス数(16 * 16 * 6 = 1536個)
	uint32_t sphereIndexCount = kSubdivision * kSubdivision * 6;

	// リソースの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> sphereIndexResource = CreateBufferResource(device.Get(), sizeof(uint32_t) * sphereIndexCount);

	// インデックスバッファビューの設定
	D3D12_INDEX_BUFFER_VIEW sphereIndexBufferView{};
	sphereIndexBufferView.BufferLocation = sphereIndexResource->GetGPUVirtualAddress();
	sphereIndexBufferView.SizeInBytes = sizeof(uint32_t) * sphereIndexCount;
	sphereIndexBufferView.Format = DXGI_FORMAT_R32_UINT;

#pragma endregion

	//===========================
	// SpriteIndexResourceの生成
	//===========================

#pragma region SpriteIndexResourceの生成

	Microsoft::WRL::ComPtr<ID3D12Resource> spriteIndexResource = CreateBufferResource(device.Get(), sizeof(uint32_t) * 6);

#pragma endregion

	//====================================
	// TransformMatrixResourceの生成
	//====================================

#pragma region TransformMatrixResourceの生成

	//================================
	// planeModelWvpResourceの生成
	//================================

#pragma region planeModelWvpResourceの生成

	// 「plane.obj」のWVP用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> planeModelWvpResource = CreateBufferResource(device.Get(), sizeof(Matrix4x4) * 2);

	// データを書き込む
	Matrix4x4* planeModelWvpData = nullptr;

	// 書き込むためのアドレスを取得
	planeModelWvpResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&planeModelWvpData));

	// HLSL側のwvpに単位行列を書き込む
	*planeModelWvpData = MathUtils::MakeIdentity4x4();

	// HLSL側のworldにも単位行列(または物体のワールド行列)を書き込む
	*(planeModelWvpData + 1) = MathUtils::MakeIdentity4x4();

#pragma endregion

	//================================
	// teapotModelWvpResourceの生成
	//================================

#pragma region teapotModelWvpResourceの生成

	// 「teapot.obj」のWVP用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> teapotModelWvpResource = CreateBufferResource(device.Get(), sizeof(Matrix4x4) * 2);

	// データを書き込む
	Matrix4x4* teapotModelWvpData = nullptr;

	// 書き込むためのアドレスを取得
	teapotModelWvpResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&teapotModelWvpData));

	// HLSL側のwvpに単位行列を書き込む
	*teapotModelWvpData = MathUtils::MakeIdentity4x4();

	// HLSL側のworldにも単位行列(または物体のワールド行列)を書き込む
	*(teapotModelWvpData + 1) = MathUtils::MakeIdentity4x4();

#pragma endregion

	//================================
	// bunnyModelWvpResourceの生成
	//================================

#pragma region bunnyModelWvpResourceの生成

	// 「bunny.obj」のWVP用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> bunnyModelWvpResource = CreateBufferResource(device.Get(), sizeof(Matrix4x4) * 2);

	// データを書き込む
	Matrix4x4* bunnyModelWvpData = nullptr;

	// 書き込むためのアドレスを取得
	bunnyModelWvpResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&bunnyModelWvpData));

	// HLSL側のwvpに単位行列を書き込む
	*bunnyModelWvpData = MathUtils::MakeIdentity4x4();

	// HLSL側のworldにも単位行列(または物体のワールド行列)を書き込む
	*(bunnyModelWvpData + 1) = MathUtils::MakeIdentity4x4();

#pragma endregion

	//==================================
	// multiMeshModelWvpResourceの生成
	//==================================

#pragma region multiMeshModelWvpResourceの生成

	// 「multiMesh.obj」のWVP用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> multiMeshModelWvpResource = CreateBufferResource(device.Get(), sizeof(Matrix4x4) * 2);

	// データを書き込む
	Matrix4x4* multiMeshModelWvpData = nullptr;

	// 書き込むためのアドレスを取得
	multiMeshModelWvpResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&multiMeshModelWvpData));

	// HLSL側のwvpに単位行列を書き込む
	*multiMeshModelWvpData = MathUtils::MakeIdentity4x4();

	// HLSL側のworldにも単位行列(または物体のワールド行列)を書き込む
	*(multiMeshModelWvpData + 1) = MathUtils::MakeIdentity4x4();

#pragma endregion

	//=====================================
	// multiMaterialModelWvpResourceの生成
	//=====================================

#pragma region multiMaterialModelWvpResourceの生成

	// 「multiMaterial.obj」のWVP用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> multiMaterialModelWvpResource = CreateBufferResource(device.Get(), sizeof(Matrix4x4) * 2);

	// データを書き込む
	Matrix4x4* multiMaterialModelWvpData = nullptr;

	// 書き込むためのアドレスを取得
	multiMaterialModelWvpResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&multiMaterialModelWvpData));

	// HLSL側のwvpに単位行列を書き込む
	*multiMaterialModelWvpData = MathUtils::MakeIdentity4x4();

	// HLSL側のworldにも単位行列(または物体のワールド行列)を書き込む
	*(multiMaterialModelWvpData + 1) = MathUtils::MakeIdentity4x4();

#pragma endregion

	//=====================================
	// suzanneModelWvpResourceの生成
	//=====================================

#pragma region suzanneModelWvpResourceの生成

	// 「suzanne.obj」のWVP用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> suzanneModelWvpResource = CreateBufferResource(device.Get(), sizeof(Matrix4x4) * 2);

	// データを書き込む
	Matrix4x4* suzanneModelWvpData = nullptr;

	// 書き込むためのアドレスを取得
	suzanneModelWvpResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&suzanneModelWvpData));

	// HLSL側のwvpに単位行列を書き込む
	*suzanneModelWvpData = MathUtils::MakeIdentity4x4();

	// HLSL側のworldにも単位行列(または物体のワールド行列)を書き込む
	*(suzanneModelWvpData + 1) = MathUtils::MakeIdentity4x4();

#pragma endregion

	//====================================
	// SphereMatrixResourceの生成
	//====================================

#pragma region SphereMatrixResourceの生成

	// SphereのWVP用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> sphereWvpResource = CreateBufferResource(device.Get(), sizeof(Matrix4x4) * 2);

	// データを書き込む
	Matrix4x4* sphereWvpData = nullptr;

	// 書き込むためのアドレスを取得
	sphereWvpResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&sphereWvpData));

	// HLSL側のwvpに単位行列を書き込む
	*sphereWvpData = MathUtils::MakeIdentity4x4();

	// HLSL側のworldにも単位行列(または物体のワールド行列)を書き込む
	*(sphereWvpData + 1) = MathUtils::MakeIdentity4x4();

	//====================================
	// SpriteMatrixResourceの生成
	//====================================

#pragma region SpriteMatrixResourceの生成

	// Sprite用もWVP用と同様にMatrix4x4 2つ分 のサイズ(128バイト)を用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> spriteTransformMatrixResource = CreateBufferResource(device.Get(), sizeof(Matrix4x4) * 2);

	// データを書き込む
	Matrix4x4* spriteTransformMatrixData = nullptr;

	// 書き込むためのアドレスを取得
	spriteTransformMatrixResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&spriteTransformMatrixData));

	// 1つ目の行列(wvp用)に単位行列を書き込む
	*spriteTransformMatrixData = MathUtils::MakeIdentity4x4();

	// 2つ目の行列(world用)にも単位行列を書き込む(+1 して次のアドレスへ)
	*(spriteTransformMatrixData + 1) = MathUtils::MakeIdentity4x4();

#pragma endregion

#pragma endregion

#pragma endregion

	//===========================
	// VertexBafferViewの設定
	//===========================

#pragma region VertexBafferViewの設定

	//=========================================
	// planeModel用の頂点バッファビューを作成する
	//=========================================

#pragma region planeModelVertexBufferView

	// 「plane.obj」モデルの頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW planeModelVertexBufferView{};

	// リソースの先頭アドレスから使う
	planeModelVertexBufferView.BufferLocation = planeModelVertexResource.Get()->GetGPUVirtualAddress();

	// VertexResourceで計算した正しいバイトサイズを設定する
	planeModelVertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * planeModelData.vertices.size());

	// 1頂点あたりのサイズ
	planeModelVertexBufferView.StrideInBytes = sizeof(VertexData);

#pragma endregion

	//=========================================
	// teapotModel用の頂点バッファビューを作成する
	//=========================================

#pragma region teapotModelVertexBufferView

	// 「teapot.obj」モデルの頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW teapotModelVertexBufferView{};

	// リソースの先頭アドレスから使う
	teapotModelVertexBufferView.BufferLocation = teapotModelVertexResource.Get()->GetGPUVirtualAddress();

	// VertexResourceで計算した正しいバイトサイズを設定する
	teapotModelVertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * teapotModelData.vertices.size());

	// 1頂点あたりのサイズ
	teapotModelVertexBufferView.StrideInBytes = sizeof(VertexData);

#pragma endregion

	//=========================================
	// bunnyModel用の頂点バッファビューを作成する
	//=========================================

#pragma region bunnyModelModelVertexBufferView

	// 「bunnyModel.obj」モデルの頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW bunnyModelVertexBufferView{};

	// リソースの先頭アドレスから使う
	bunnyModelVertexBufferView.BufferLocation = bunnyModelVertexResource.Get()->GetGPUVirtualAddress();

	// VertexResourceで計算した正しいバイトサイズを設定する
	bunnyModelVertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * bunnyModelData.vertices.size());

	// 1頂点あたりのサイズ
	bunnyModelVertexBufferView.StrideInBytes = sizeof(VertexData);

#pragma endregion

	//============================================
	// multiMeshModel用の頂点バッファビューを作成する
	//============================================

#pragma region multiMeshModelVertexBufferView

	// 「multiMesh.obj」モデルの頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW multiMeshModelVertexBufferView{};

	// リソースの先頭アドレスから使う
	multiMeshModelVertexBufferView.BufferLocation = multiMeshModelVertexResource.Get()->GetGPUVirtualAddress();

	// VertexResourceで計算した正しいバイトサイズを設定する
	multiMeshModelVertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * multiMeshModelData.vertices.size());

	// 1頂点あたりのサイズ
	multiMeshModelVertexBufferView.StrideInBytes = sizeof(VertexData);

#pragma endregion

	//=================================================
	// multiMaterialModel用の頂点バッファビューを作成する
	//=================================================

#pragma region multiMaterialModelVertexBufferView

	// 「multiMaterial.obj」モデルの頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW multiMaterialModelVertexBufferView{};

	// リソースの先頭アドレスから使う
	multiMaterialModelVertexBufferView.BufferLocation = multiMaterialModelVertexResource.Get()->GetGPUVirtualAddress();

	// VertexResourceで計算した正しいバイトサイズを設定する
	multiMaterialModelVertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * multiMaterialModelData.vertices.size());

	// 1頂点あたりのサイズ
	multiMaterialModelVertexBufferView.StrideInBytes = sizeof(VertexData);

#pragma endregion

	//=================================================
	// suzanneModel用の頂点バッファビューを作成する
	//=================================================

#pragma region suzanneModelVertexBufferView

	// 「suzannel.obj」モデルの頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW suzanneModelVertexBufferView{};

	// リソースの先頭アドレスから使う
	suzanneModelVertexBufferView.BufferLocation = suzanneModelVertexResource.Get()->GetGPUVirtualAddress();

	// VertexResourceで計算した正しいバイトサイズを設定する
	suzanneModelVertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * suzanneModelData.vertices.size());

	// 1頂点あたりのサイズ
	suzanneModelVertexBufferView.StrideInBytes = sizeof(VertexData);

#pragma endregion

	//=====================================
	// Sphere用の頂点バッファビューを作成する
	//=====================================

#pragma region Sphere用の頂点バッファビューを作成する

	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW sphereVertexBufferView{};

	// リソースの先頭アドレスから使う
	sphereVertexBufferView.BufferLocation = sphereVertexResource.Get()->GetGPUVirtualAddress();

	// VertexResourceで計算した正しいバイトサイズを設定する
	sphereVertexBufferView.SizeInBytes = static_cast<UINT>(sizeInBytes);

	// 1頂点あたりのサイズ
	sphereVertexBufferView.StrideInBytes = sizeof(VertexData);

#pragma endregion

	//=====================================
	// Sprite用の頂点バッファビューを作成する
	//=====================================

#pragma region Sprite用の頂点バッファビューを作成する

	D3D12_VERTEX_BUFFER_VIEW spriteVertexBufferView{};

	// リソースの先頭アドレスから使う
	spriteVertexBufferView.BufferLocation = spriteVertexResource->GetGPUVirtualAddress();

	// 1頂点あたりのサイズ
	spriteVertexBufferView.StrideInBytes = sizeof(VertexData);

	// 使用するリソースのサイズは頂点4つ分のサイズ
	spriteVertexBufferView.SizeInBytes = sizeof(VertexData) * 4;

#pragma endregion

#pragma endregion

	//==============================
	// SpriteIndexBufferViewの設定
	//==============================

#pragma region SpriteIndexBufferViewの設定

	D3D12_INDEX_BUFFER_VIEW spriteIndexBufferView{};

	// リソースの先頭アドレスから使う
	spriteIndexBufferView.BufferLocation = spriteIndexResource->GetGPUVirtualAddress();

	// 使用するリソースのサイズはインデックス6つ分のサイズ
	spriteIndexBufferView.SizeInBytes = sizeof(uint32_t) * 6;

	// インデックスは uint32_t とする
	spriteIndexBufferView.Format = DXGI_FORMAT_R32_UINT;

#pragma endregion

	//================================
	// Resourceに頂点データを書きこむ
	//================================

#pragma region Resourceに頂点データを書きこむ

	//=========================================
	// plane.objの頂点リソースにデータを書き込む
	//=========================================

#pragma region plane.objの頂点リソースにデータを書き込む

	VertexData* planeModelVertexData = nullptr;

	// 書き込むためのアドレスを取得する
	planeModelVertexResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&planeModelVertexData));

	// 頂点データをリソースにコピー
	std::memcpy(planeModelVertexData, planeModelData.vertices.data(), sizeof(VertexData) * planeModelData.vertices.size());
	planeModelVertexResource.Get()->Unmap(0, nullptr);

#pragma endregion

	//=========================================
	// teapot.objの頂点リソースにデータを書き込む
	//=========================================

#pragma region teapot.objの頂点リソースにデータを書き込む

	VertexData* teapotModelVertexData = nullptr;

	// 書き込むためのアドレスを取得する
	teapotModelVertexResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&teapotModelVertexData));

	// 頂点データをリソースにコピー
	std::memcpy(teapotModelVertexData, teapotModelData.vertices.data(), sizeof(VertexData) * teapotModelData.vertices.size());
	teapotModelVertexResource.Get()->Unmap(0, nullptr);

#pragma endregion

	//=========================================
	// bunny.objの頂点リソースにデータを書き込む
	//=========================================

#pragma region  bunny.objの頂点リソースにデータを書き込む

	VertexData* bunnyModelVertexData = nullptr;

	// 書き込むためのアドレスを取得する
	bunnyModelVertexResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&bunnyModelVertexData));

	// 頂点データをリソースにコピー
	std::memcpy(bunnyModelVertexData, bunnyModelData.vertices.data(), sizeof(VertexData) * bunnyModelData.vertices.size());
	bunnyModelVertexResource.Get()->Unmap(0, nullptr);

#pragma endregion

	//============================================
	// multiMesh.objの頂点リソースにデータを書き込む
	//============================================

#pragma region multiMesh.objの頂点リソースにデータを書き込む

	VertexData* multiMeshModelVertexData = nullptr;

	// 書き込むためのアドレスを取得する
	multiMeshModelVertexResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&multiMeshModelVertexData));

	// 頂点データをリソースにコピー
	std::memcpy(multiMeshModelVertexData, multiMeshModelData.vertices.data(), sizeof(VertexData) * multiMeshModelData.vertices.size());
	multiMeshModelVertexResource.Get()->Unmap(0, nullptr);

#pragma endregion

	//===============================================
	// multiMaterial.objの頂点リソースにデータを書き込む
	//===============================================

#pragma region multiMaterial.objの頂点リソースにデータを書き込む

	VertexData* multiMaterialModelVertexData = nullptr;

	// 書き込むためのアドレスを取得する
	multiMaterialModelVertexResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&multiMaterialModelVertexData));

	// 頂点データをリソースにコピー
	std::memcpy(multiMaterialModelVertexData, multiMaterialModelData.vertices.data(), sizeof(VertexData) * multiMaterialModelData.vertices.size());
	multiMaterialModelVertexResource.Get()->Unmap(0, nullptr);

#pragma endregion

	//===============================================
	// suzanne.objの頂点リソースにデータを書き込む
	//===============================================

#pragma region suzanne.objの頂点リソースにデータを書き込む

	VertexData* suzanneModelVertexData = nullptr;

	// 書き込むためのアドレスを取得する
	suzanneModelVertexResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&suzanneModelVertexData));

	// 頂点データをリソースにコピー
	std::memcpy(suzanneModelVertexData, suzanneModelData.vertices.data(), sizeof(VertexData) * suzanneModelData.vertices.size());
	suzanneModelVertexResource.Get()->Unmap(0, nullptr);

#pragma endregion

	//===============================
	// Sprite用の頂点データを書き込む
	//===============================

#pragma region Sprite用の頂点データを書き込む

	VertexData* spriteVertexData = nullptr;

	// 書き込むためのアドレスを取得する
	spriteVertexResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&spriteVertexData));

	// 幅 640、高さ 360 の矩形を左上原点(0, 0)基準で作成
	float w = 640.0f;
	float h = 360.0f;

	// 左下
	spriteVertexData[0].position = { 0.0f, h, 0.0f, 1.0f };
	spriteVertexData[0].texcoord = { 0.0f, 1.0f };

	// 左上
	spriteVertexData[1].position = { 0.0f, 0.0f, 0.0f, 1.0f };
	spriteVertexData[1].texcoord = { 0.0f, 0.0f };

	// 右下
	spriteVertexData[2].position = { w, h, 0.0f, 1.0f };
	spriteVertexData[2].texcoord = { 1.0f, 1.0f };

	// 左上
	spriteVertexData[3].position = { w, 0.0f, 0.0f, 1.0f };
	spriteVertexData[3].texcoord = { 1.0f, 0.0f };

	// 全頂点に対して法線を設定(Sprite用)
	for (int i = 0; i < 4; i++) {
		spriteVertexData[i].normal = { 0.0f, 0.0f, -1.0f };
	}

	// VertexResourceSpriteのアンマップ
	spriteVertexResource.Get()->Unmap(0, nullptr);

#pragma endregion

	//===============================
	// Sphere用の頂点データを書き込む
	//===============================

#pragma region Sphere用の頂点データを書き込む

	// 頂点リソースにデータを書き込む
	VertexData* sphereVertexData = nullptr;

	// 書き込むためのアドレスを取得する
	sphereVertexResource->Map(0, nullptr, reinterpret_cast<void**>(&sphereVertexData));

	// === 球体の設定 ===

	// 経度分割1つ分の角度(全周 2π を 分割数 で割る)
	const float kLonEvery = (2.0f * static_cast<float>(M_PI)) / static_cast<float>(kSubdivision);

	// 緯度分割1つ分の角度(半周 π を 分割数 で割る)
	const float kLatEvery = static_cast<float>(M_PI) / static_cast<float>(kSubdivision);

	uint32_t vIndex = 0;

	// 緯度の方向に分割
	for (uint32_t latIndex = 0; latIndex <= kSubdivision; latIndex++) {

		// 範囲を -π/2 ~ π/2 にするための計算
		float lat = -static_cast<float>(M_PI) / 2.0f + kLatEvery * latIndex;

		// 経度の方向に分割
		for (uint32_t lonIndex = 0; lonIndex <= kSubdivision; lonIndex++) {

			// 現在の経度 lon
			float lon = lonIndex * kLonEvery;

			// 頂点座標の計算
			Vector4 p = { cosf(lat) * cosf(lon), sinf(lat), cosf(lat) * sinf(lon), 1.0f };

			sphereVertexData[vIndex].position = p;

			sphereVertexData[vIndex].texcoord = {
				static_cast<float>(lonIndex) / kSubdivision,
				1.0f - static_cast<float>(latIndex) / kSubdivision
			};

			sphereVertexData[vIndex].normal = { p.x, p.y, p.z };

			// 1つずつ順番に詰めていく
			vIndex++;
		}
	}

	// VertexResourceのアンマップ
	sphereVertexResource.Get()->Unmap(0, nullptr);

	// 球体のインデックスデータを書き込む //
	uint32_t* sphereIndexData = nullptr;
	sphereIndexResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&sphereIndexData));

	uint32_t indexOffset = 0;
	uint32_t rowLength = kSubdivision + 1;

	for (uint32_t latIndex = 0; latIndex < kSubdivision; latIndex++) {
		for (uint32_t lonIndex = 0; lonIndex < kSubdivision; lonIndex++) {

			// 4隅の頂点番号を計算
			uint32_t lb = latIndex * rowLength + lonIndex;
			uint32_t lt = (latIndex + 1) * rowLength + lonIndex;
			uint32_t rb = lb + 1;
			uint32_t rt = lt + 1;

			// 1枚目の三角形
			sphereIndexData[indexOffset + 0] = lb;
			sphereIndexData[indexOffset + 1] = lt;
			sphereIndexData[indexOffset + 2] = rb;

			// 2枚目の三角形
			sphereIndexData[indexOffset + 3] = lt;
			sphereIndexData[indexOffset + 4] = rt;
			sphereIndexData[indexOffset + 5] = rb;

			indexOffset += 6;
		}
	}
	sphereIndexResource.Get()->Unmap(0, nullptr);

#pragma endregion

	//=====================================
	// SpriteIndexResourceにデータを書き込む
	//=====================================

#pragma region SpriteIndexResourceにデータを書き込む

	// インデックスリソースにデータを書き込む
	uint32_t* spriteIndexData = nullptr;

	spriteIndexResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&spriteIndexData));
	spriteIndexData[0] = 0;
	spriteIndexData[1] = 1;
	spriteIndexData[2] = 2;
	spriteIndexData[3] = 1;
	spriteIndexData[4] = 3;
	spriteIndexData[5] = 2;

#pragma endregion

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

	// plane.obj用のTransformを作成する
	Transform planeModelTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, static_cast<float>(M_PI), 0.0f}, {0.0f, 0.0f, 0.0f} };

	// Sphere用のTransformを作成する
	Transform sphereTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, static_cast<float>(M_PI), 0.0f}, {0.0f, 0.0f, 0.0f} };

	// teapot.obj用のTransformを作成する
	Transform teapotModelTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, static_cast<float>(M_PI), 0.0f}, {0.0f, 0.0f, 0.0f} };

	// bunny.obj用のTransformを作成する
	Transform bunnyModelTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, static_cast<float>(M_PI), 0.0f}, {0.0f, 0.0f, 0.0f} };

	// multiMesh.obj用のTransformを作成する
	Transform multiMeshModelTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, static_cast<float>(M_PI), 0.0f}, {0.0f, 0.0f, 0.0f} };

	// multiMaterial.obj用のTransformを作成する
	Transform multiMaterialModelTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, static_cast<float>(M_PI), 0.0f}, {0.0f, 0.0f, 0.0f} };

	// multiMaterial.obj用のTransformを作成する
	Transform suzanneModelTransform{ {1.0f, 1.0f, 1.0f}, {0.0f, static_cast<float>(M_PI), 0.0f}, {0.0f, 0.0f, 0.0f} };

	// Sprite用のTransformを作成する
	Transform spriteTransform{ {1.0f, 1.0f, 1.0f},{0.0f, 0.0f, 0.0f},{0.0f, 0.0f, 0.0f} };

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
	ImGui_ImplDX12_Init(device.Get(), swapChainDesc.BufferCount, rtvDesc.Format, srvDescriptorHeap.Get(), srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();
#endif

#pragma endregion

	//=======================
	// デバッグカメラの初期化
	//=======================

#pragma region デバッグカメラの初期化

	// デバッグカメラを使うかどうかのフラグ
	bool useDebugCamera = false;

	// デバッグカメラのインスタンス生成と初期化
	std::unique_ptr<DebugCamera> debugCamera = std::make_unique<DebugCamera>();
	debugCamera->Initialize();

#pragma endregion

	//============================
	// Textureを読み込んで転送する
	//============================

#pragma region Textureを読み込んで転送する

	//==========================================
	// 「plane.obj」のモデルのテクスチャを読み込む
	//==========================================

	DirectX::ScratchImage planeModelMipImages = LoadTexture(planeModelData.material.textureFilePath);
	const DirectX::TexMetadata& planeModelMetadata = planeModelMipImages.GetMetadata();

	// リソース作成
	Microsoft::WRL::ComPtr<ID3D12Resource> planeModelTextureResource = CreateTextureResource(device.Get(), planeModelMetadata);

	//==========================================
	// 「teapot.obj」のモデルのテクスチャを読み込む
	//==========================================

	DirectX::ScratchImage teapotModelMipImages = LoadTexture(teapotModelData.material.textureFilePath);
	const DirectX::TexMetadata& teapotModelMetadata = teapotModelMipImages.GetMetadata();

	// リソース作成
	Microsoft::WRL::ComPtr<ID3D12Resource> teapotModelTextureResource = CreateTextureResource(device.Get(), teapotModelMetadata);

	//==========================================
	// 「bunny.obj」のモデルのテクスチャを読み込む
	//==========================================

	DirectX::ScratchImage bunnyModelMipImages = LoadTexture(bunnyModelData.material.textureFilePath);
	const DirectX::TexMetadata& bunnyModelMetadata = bunnyModelMipImages.GetMetadata();

	// リソース作成
	Microsoft::WRL::ComPtr<ID3D12Resource> bunnyModelTextureResource = CreateTextureResource(device.Get(), bunnyModelMetadata);

	//=============================================
	// 「multiMesh.obj」のモデルのテクスチャを読み込む
	//=============================================

	DirectX::ScratchImage multiMeshModelMipImages = LoadTexture(multiMeshModelData.material.textureFilePath);
	const DirectX::TexMetadata& multiMeshModelMetadata = multiMeshModelMipImages.GetMetadata();

	// リソース作成
	Microsoft::WRL::ComPtr<ID3D12Resource> multiMeshModelTextureResource = CreateTextureResource(device.Get(), multiMeshModelMetadata);

	//=================================================
	// 「multiMaterial.obj」のモデルのテクスチャを読み込む
	//=================================================

	DirectX::ScratchImage multiMaterialModelMipImages = LoadTexture(multiMaterialModelData.material.textureFilePath);
	const DirectX::TexMetadata& multiMaterialModelMetadata = multiMaterialModelMipImages.GetMetadata();

	// リソース作成
	Microsoft::WRL::ComPtr<ID3D12Resource> multiMaterialModelTextureResource = CreateTextureResource(device.Get(), multiMaterialModelMetadata);

	//==========================================
	// monsterBallのテクスチャを読み込む
	//==========================================

	DirectX::ScratchImage monsterBallMipImages = LoadTexture("resources/monsterBall.png");
	const DirectX::TexMetadata& monsterBallMetadata = monsterBallMipImages.GetMetadata();

	// リソース作成
	Microsoft::WRL::ComPtr<ID3D12Resource> monsterBallTextureResource = CreateTextureResource(device.Get(), monsterBallMetadata);

	//=============================
	// Sphereのテクスチャを読み込む
	//=============================

	DirectX::ScratchImage sphereMipImages = LoadTexture("resources/uvChecker.png");
	const DirectX::TexMetadata& sphereMetadata = sphereMipImages.GetMetadata();

	// リソース作成
	Microsoft::WRL::ComPtr<ID3D12Resource> sphereTextureResource = CreateTextureResource(device.Get(), sphereMetadata);

	//==========================================
	// Spriteのテクスチャを読み込む
	//==========================================

	DirectX::ScratchImage spriteMipImages = LoadTexture("resources/uvChecker.png");
	const DirectX::TexMetadata& spriteMetadata = spriteMipImages.GetMetadata();

	// リソース作成
	Microsoft::WRL::ComPtr<ID3D12Resource> spriteTextureResource = CreateTextureResource(device.Get(), spriteMetadata);

#pragma endregion

	//===========================
	// コマンドを実行して完了を待つ
	//===========================

#pragma region コマンドを実行して完了を待つ

	// 転送関数を呼び出し、コピーコマンドをコマンドリストに積む(中間リソースが戻る) //

	// 「plane.obj」の転送
	Microsoft::WRL::ComPtr<ID3D12Resource> planeModelIntermediateResource = UploadTextureData(planeModelTextureResource.Get(), planeModelMipImages, device.Get(), commandList.Get());

	// 「teapot.obj」の転送
	Microsoft::WRL::ComPtr<ID3D12Resource> teapotModelIntermediateResource = UploadTextureData(teapotModelTextureResource.Get(), teapotModelMipImages, device.Get(), commandList.Get());

	// 「bunny.obj」の転送
	Microsoft::WRL::ComPtr<ID3D12Resource> bunnyModelIntermediateResource = UploadTextureData(bunnyModelTextureResource.Get(), bunnyModelMipImages, device.Get(), commandList.Get());

	// 「multiMesh.obj」の転送
	Microsoft::WRL::ComPtr<ID3D12Resource> multiMeshModelIntermediateResource = UploadTextureData(multiMeshModelTextureResource.Get(), multiMeshModelMipImages, device.Get(), commandList.Get());

	// 「multiMaterial.obj」の転送
	Microsoft::WRL::ComPtr<ID3D12Resource> multiMaterialModelIntermediateResource = UploadTextureData(multiMaterialModelTextureResource.Get(), multiMaterialModelMipImages, device.Get(), commandList.Get());

	// 「monsterBall」の転送
	Microsoft::WRL::ComPtr<ID3D12Resource> monsterBallIntermediateResource = UploadTextureData(monsterBallTextureResource.Get(), monsterBallMipImages, device.Get(), commandList.Get());

	// 「sphere」の転送
	Microsoft::WRL::ComPtr<ID3D12Resource> sphereIntermediateResource = UploadTextureData(sphereTextureResource.Get(), sphereMipImages, device.Get(), commandList.Get());

	// 「sprite」の転送
	Microsoft::WRL::ComPtr<ID3D12Resource> spriteIntermediateResource = UploadTextureData(spriteTextureResource.Get(), spriteMipImages, device.Get(), commandList.Get());

#pragma endregion

#pragma endregion

	//===========================================
	// 「plane.obj」のテクスチャのSRVを作成
	//===========================================

	// metaDataを基にSRVを作成
	D3D12_SHADER_RESOURCE_VIEW_DESC planeModelSrvDesc{};
	planeModelSrvDesc.Format = planeModelMetadata.format;
	planeModelSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// 2Dテクスチャ
	planeModelSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

	// テクスチャリソースが持っているすべてのミップマップレベルを自動的にすべて割り当てる(符号なし整数の最大値を直接表す「0xFFFFFFFF」に書き換える)
	planeModelSrvDesc.Texture2D.MipLevels = 0xFFFFFFFF;

	// SRVを作成するDescriptorHeapの場所を決める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU1 = srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU1 = srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

	// 先頭はImGuiが使っているのでその次を使う
	textureSrvHandleCPU1.ptr += descriptorSizeSRV;
	textureSrvHandleGPU1.ptr += descriptorSizeSRV;

	// SRVを作成(インデックス1の場所に書き込まれる)
	device->CreateShaderResourceView(planeModelTextureResource.Get(), &planeModelSrvDesc, textureSrvHandleCPU1);

	//===========================================
	// SpriteのテクスチャのSRVを作成
	//===========================================

	D3D12_SHADER_RESOURCE_VIEW_DESC spriteSrvDesc{};
	spriteSrvDesc.Format = spriteMetadata.format;
	spriteSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// 2Dテクスチャ
	spriteSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	spriteSrvDesc.Texture2D.MipLevels = 0xFFFFFFFF;

	// インデックス1(textureSrvHandleCPU1)の次の場所(インデックス2)に配置するため、1つ分のアドレスを進める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = textureSrvHandleCPU1;
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = textureSrvHandleGPU1;

	textureSrvHandleCPU2.ptr += descriptorSizeSRV;
	textureSrvHandleGPU2.ptr += descriptorSizeSRV;

	// SRVを作成
	device->CreateShaderResourceView(spriteTextureResource.Get(), &spriteSrvDesc, textureSrvHandleCPU2);

	//===========================================
	// SphereのテクスチャのSRVを作成
	//===========================================

	D3D12_SHADER_RESOURCE_VIEW_DESC sphereSrvDesc{};
	sphereSrvDesc.Format = sphereMetadata.format;
	sphereSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// 2Dテクスチャ
	sphereSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	sphereSrvDesc.Texture2D.MipLevels = 0xFFFFFFFF;

	// インデックス2(textureSrvHandleCPU2)の次の場所(インデックス3)に配置するため、1つ分のアドレスを進める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU3 = textureSrvHandleCPU2;
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU3 = textureSrvHandleGPU2;

	textureSrvHandleCPU3.ptr += descriptorSizeSRV;
	textureSrvHandleGPU3.ptr += descriptorSizeSRV;

	// SRVを作成(インデックス3の場所に書き込まれる)
	device->CreateShaderResourceView(sphereTextureResource.Get(), &sphereSrvDesc, textureSrvHandleCPU3);

	//===========================================
	// teapotのテクスチャのSRVを作成
	//===========================================

	D3D12_SHADER_RESOURCE_VIEW_DESC teapotSrvDesc{};
	teapotSrvDesc.Format = teapotModelMetadata.format;
	teapotSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// 2Dテクスチャ
	teapotSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	teapotSrvDesc.Texture2D.MipLevels = 0xFFFFFFFF;

	// インデックス3(textureSrvHandleCPU3)の次の場所(インデックス4)に配置するため、1つ分のアドレスを進める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU4 = textureSrvHandleCPU3;
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU4 = textureSrvHandleGPU3;

	textureSrvHandleCPU4.ptr += descriptorSizeSRV;
	textureSrvHandleGPU4.ptr += descriptorSizeSRV;

	// SRVを作成(インデックス4の場所に書き込まれる)
	device->CreateShaderResourceView(teapotModelTextureResource.Get(), &teapotSrvDesc, textureSrvHandleCPU4);

	//===========================================
	// bunnyのテクスチャのSRVを作成
	//===========================================

	D3D12_SHADER_RESOURCE_VIEW_DESC bunnySrvDesc{};
	bunnySrvDesc.Format = bunnyModelMetadata.format;
	bunnySrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// 2Dテクスチャ
	bunnySrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	bunnySrvDesc.Texture2D.MipLevels = 0xFFFFFFFF;

	// インデックス4(textureSrvHandleCPU4)の次の場所(インデックス5)に配置するため、1つ分のアドレスを進める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU5 = textureSrvHandleCPU4;
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU5 = textureSrvHandleGPU4;

	textureSrvHandleCPU5.ptr += descriptorSizeSRV;
	textureSrvHandleGPU5.ptr += descriptorSizeSRV;

	// SRVを作成(インデックス5の場所に書き込まれる)
	device->CreateShaderResourceView(bunnyModelTextureResource.Get(), &bunnySrvDesc, textureSrvHandleCPU5);

	//===========================================
	// multiMeshのテクスチャのSRVを作成
	//===========================================

	D3D12_SHADER_RESOURCE_VIEW_DESC multiMeshSrvDesc{};
	multiMeshSrvDesc.Format = multiMeshModelMetadata.format;
	multiMeshSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// 2Dテクスチャ
	multiMeshSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	multiMeshSrvDesc.Texture2D.MipLevels = 0xFFFFFFFF;

	// インデックス5(textureSrvHandleCPU5)の次の場所(インデックス6)に配置するため、1つ分のアドレスを進める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU6 = textureSrvHandleCPU5;
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU6 = textureSrvHandleGPU5;

	textureSrvHandleCPU6.ptr += descriptorSizeSRV;
	textureSrvHandleGPU6.ptr += descriptorSizeSRV;

	// SRVを作成(インデックス6の場所に書き込まれる)
	device->CreateShaderResourceView(multiMeshModelTextureResource.Get(), &multiMeshSrvDesc, textureSrvHandleCPU6);

	//===========================================
	// multiMaterialのテクスチャのSRVを作成
	//===========================================

	D3D12_SHADER_RESOURCE_VIEW_DESC multiMaterialSrvDesc{};
	multiMaterialSrvDesc.Format = multiMaterialModelMetadata.format;
	multiMaterialSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// 2Dテクスチャ
	multiMaterialSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	multiMaterialSrvDesc.Texture2D.MipLevels = 0xFFFFFFFF;

	// インデックス6(textureSrvHandleCPU6)の次の場所(インデックス7)に配置するため、1つ分のアドレスを進める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU7 = textureSrvHandleCPU6;
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU7 = textureSrvHandleGPU6;

	textureSrvHandleCPU7.ptr += descriptorSizeSRV;
	textureSrvHandleGPU7.ptr += descriptorSizeSRV;

	// SRVを作成(インデックス7の場所に書き込まれる)
	device->CreateShaderResourceView(multiMaterialModelTextureResource.Get(), &multiMaterialSrvDesc, textureSrvHandleCPU7);

	//===========================================
	// monsterBallのテクスチャのSRVを作成
	//===========================================

	D3D12_SHADER_RESOURCE_VIEW_DESC monsterBallSrvDesc{};
	monsterBallSrvDesc.Format = monsterBallMetadata.format;
	monsterBallSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// 2Dテクスチャ
	monsterBallSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	monsterBallSrvDesc.Texture2D.MipLevels = 0xFFFFFFFF;

	// インデックス7(textureSrvHandleCPU7)の次の場所(インデックス8)に配置するため、1つ分のアドレスを進める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU8 = textureSrvHandleCPU7;
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU8 = textureSrvHandleGPU7;

	textureSrvHandleCPU8.ptr += descriptorSizeSRV;
	textureSrvHandleGPU8.ptr += descriptorSizeSRV;

	// SRVを作成(インデックス8の場所に書き込まれる)
	device->CreateShaderResourceView(monsterBallTextureResource.Get(), &monsterBallSrvDesc, textureSrvHandleCPU8);

	//================
	// サウンドの再生
	//================

#pragma region サウンドの再生

	audioManager->SoundPlayWave(soundData1);

#pragma endregion

	//============================
	// スプライトの位置を保持する変数
	//============================

#pragma region スプライトの位置を保持する変数

	// カメラのTransform(Zの初期値を -10.0f に設定)
	Transform cameraTransform{ {1.0f, 1.0f, 1.0f},{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -10.0f} };

#pragma endregion

	//=======================
	// ImGuiの切り替え用の変数
	//=======================

	// 0: plane.obj
	int currentModelIndex = 0;
	bool isSpriteVisible = true;

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
		} else {

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
			ImGui::Begin("Settings");

			//===========================================
			// Camera セクション
			//===========================================

			if (ImGui::CollapsingHeader("Camera")) {

				// デバッグカメラへの切り替え
				ImGui::Checkbox("Use Debug Camera", &useDebugCamera);

				// デバッグカメラのリセット
				if (useDebugCamera) {
					if (ImGui::Button("Reset Debug Camera")) {
						debugCamera->Reset();
					}
				}
			}

			//===========================================
			// Model セクション(Lighting項目を内包)
			//===========================================

			if (ImGui::CollapsingHeader("Model Object", ImGuiTreeNodeFlags_DefaultOpen)) {

				// モデル選択
				ImGui::Text("-Select Model-");
				ImGui::RadioButton("Plane (plane.obj)", &currentModelIndex, 0);
				ImGui::RadioButton("Sphere (sphere.obj)", &currentModelIndex, 1);
				ImGui::RadioButton("Utah Teapot (teapot.obj)", &currentModelIndex, 2);
				ImGui::RadioButton("Stanford Bunny (bunny.obj)", &currentModelIndex, 3);
				ImGui::RadioButton("MultiMesh (multiMesh.obj)", &currentModelIndex, 4);
				ImGui::RadioButton("MultiMaterial (multiMaterial.obj)", &currentModelIndex, 5);
				ImGui::RadioButton("Suzanne (suzanne.obj)", &currentModelIndex, 6);

				// 選択中のモデルに応じてポインタを切り替え
				Transform* currentTransform = &planeModelTransform;
				Material* currentMaterialData = planeModelMaterialData;

				// switch文での参照ポインタ切替
				switch (currentModelIndex) {
				case 0:
					currentTransform = &planeModelTransform;
					currentMaterialData = planeModelMaterialData;
					break;
				case 1:
					currentTransform = &sphereTransform;
					currentMaterialData = sphereMaterialData;
					break;
				case 2:
					currentTransform = &teapotModelTransform;
					currentMaterialData = teapotModelMaterialData;
					break;
				case 3:
					currentTransform = &bunnyModelTransform;
					currentMaterialData = bunnyModelMaterialData;
					break;
				case 4:
					currentTransform = &multiMeshModelTransform;
					currentMaterialData = multiMeshModelMaterialData;
					break;
				case 5:
					currentTransform = &multiMaterialModelTransform;
					currentMaterialData = multiMaterialModelMaterialData;
					break;
				case 6:
					currentTransform = &suzanneModelTransform;
					currentMaterialData = suzanneModelMaterialData;
					break;
				}

				ImGui::Separator();

				// Transform 操作
				ImGui::Text("-Transform-");
				ImGui::DragFloat3("Translate", &currentTransform->translate.x, 0.01f);
				ImGui::DragFloat3("Rotate", &currentTransform->rotate.x, 0.01f);
				ImGui::DragFloat3("Scale", &currentTransform->scale.x, 0.01f);

				if (ImGui::Button("Reset Model Transform")) {
					if (currentModelIndex == 1) {
						// Sphere
						sphereTransform.translate = { 0.0f, 0.0f, 0.0f };
						sphereTransform.rotate = { 0.0f, 0.0f, 0.0f };
						sphereTransform.scale = { 1.0f, 1.0f, 1.0f };
					} else if (currentModelIndex == 0) {
						// Plane
						planeModelTransform.translate = { 0.0f, 0.0f, 0.0f };
						planeModelTransform.rotate = { 0.0f, static_cast<float>(M_PI), 0.0f };
						planeModelTransform.scale = { 1.0f, 1.0f, 1.0f };
					} else if (currentModelIndex == 4) {
						// multiMesh
						multiMeshModelTransform.translate = { 0.0f, 0.0f, 0.0f };
						multiMeshModelTransform.rotate = { 0.0f, static_cast<float>(M_PI), 0.0f };
						multiMeshModelTransform.scale = { 1.0f, 1.0f, 1.0f };
					} else if (currentModelIndex == 5) {
						// multiMaterial
						multiMaterialModelTransform.translate = { 0.0f, 0.0f, 0.0f };
						multiMaterialModelTransform.rotate = { 0.0f, static_cast<float>(M_PI), 0.0f };
						multiMaterialModelTransform.scale = { 1.0f, 1.0f, 1.0f };
					} else {
						currentTransform->translate = { 0.0f, 0.0f, 0.0f };
						currentTransform->rotate = { 0.0f, static_cast<float>(M_PI), 0.0f };
						currentTransform->scale = { 1.0f, 1.0f, 1.0f };
					}
				}

				// Material & Lighting 設定
				ImGui::Text("-Model Material-");

				// カラー編集
				ImGui::ColorEdit3("Model Color", &currentMaterialData->color.x);

				// マテリアルカラーのリセットボタン
				if (ImGui::Button("Reset Model Material")) {
					// 白(RGBA: 1, 1, 1, 1)にリセット
					currentMaterialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
				}

				ImGui::Separator();
				ImGui::Text("-Lighting Model-");

				// ライティングモード切替
				ImGui::RadioButton("None (Disabled)", &currentMaterialData->lightingMode, 0);
				ImGui::RadioButton("Lambertian Reflectance", &currentMaterialData->lightingMode, 1);
				ImGui::RadioButton("Half-Lambert", &currentMaterialData->lightingMode, 2);

				// ライティングが有効(1: Lambertian または 2: Half-Lambert)の時だけライトパラメータを表示
				if (currentMaterialData->lightingMode != 0) {

					ImGui::Separator();
					ImGui::Text("-Directional Light Settings-");

					ImGui::ColorEdit3("Light Color", &directionalLightData->color.x);
					ImGui::SliderFloat("Intensity", &directionalLightData->intensity, 0.0f, 10.0f);

					if (ImGui::DragFloat3("Light Direction", &directionalLightData->direction.x, 0.01f)) {
						directionalLightData->direction = MathUtils::Normalize(directionalLightData->direction);
					}

					// Lightのリセットボタン
					if (ImGui::Button("Reset Light Settings")) {
						directionalLightData->color.x = 1.0f;
						directionalLightData->color.y = 1.0f;
						directionalLightData->color.z = 1.0f;
						directionalLightData->intensity = 1.0f;
						directionalLightData->direction = { 0.0f, -1.0f, 0.0f };
					}
				}
			}

			//===========================================
			// Sprite セクション
			//===========================================

			if (ImGui::CollapsingHeader("Sprite Object", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Checkbox("Toggling Sprite Display", &isSpriteVisible);

				ImGui::Separator();

				// Sprite Transform
				ImGui::Text("-Transform-");
				ImGui::DragFloat2("Translate##SpriteTranslate", &spriteTransform.translate.x, 1.0f);
				ImGui::SliderAngle("Rotate##SpriteRotate", &spriteTransform.rotate.z);
				ImGui::DragFloat2("Scale##SpriteScale", &spriteTransform.scale.x, 0.01f);

				if (ImGui::Button("Reset Sprite Transform")) {
					spriteTransform.translate = { 0.0f, 0.0f, 0.0f };
					spriteTransform.rotate = { 0.0f, 0.0f, 0.0f };
					spriteTransform.scale = { 1.0f, 1.0f, 1.0f };
				}

				// Sprite Material & UV
				ImGui::Text("-Sprite Material-");

				ImGui::ColorEdit3("Sprite Color", &spriteMaterialData->color.x);

				ImGui::Separator();

				ImGui::Text("[UV Transform]");
				ImGui::DragFloat2("UV Translate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
				ImGui::SliderAngle("UV Rotate", &uvTransformSprite.rotate.z);
				ImGui::DragFloat2("UV Scale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);

				// Material & UV Transform のリセットボタン
				if (ImGui::Button("Reset Material & UV")) {

					// マテリアルカラーを白(1.0f, 1.0f, 1.0f)にリセット
					spriteMaterialData->color.x = 1.0f;
					spriteMaterialData->color.y = 1.0f;
					spriteMaterialData->color.z = 1.0f;

					// UV Transform を初期値にリセット
					uvTransformSprite.translate = { 0.0f, 0.0f };
					uvTransformSprite.rotate = { 0.0f, 0.0f, 0.0f };
					uvTransformSprite.scale = { 1.0f, 1.0f };
				}
			}
			ImGui::End();
#endif

			//===================================
			// フレームの先頭で入力を更新
			//===================================

			// 入力の更新
			input->Update();

			// 現在選択されているモデルの Transform ポインタを取得
			Transform* activeTransform = &planeModelTransform;

			switch (currentModelIndex) {
			case 0:
				activeTransform = &planeModelTransform;
				break;
			case 1:
				activeTransform = &sphereTransform;
				break;
			case 2:
				activeTransform = &teapotModelTransform;
				break;
			case 3:
				activeTransform = &bunnyModelTransform;
				break;
			case 4:
				activeTransform = &multiMeshModelTransform;
				break;
			case 5:
				activeTransform = &multiMaterialModelTransform;
				break;
			case 6:
				activeTransform = &suzanneModelTransform;
				break;
			}

			// Xboxコントローラーによる Transform 操作
			if (input->IsGamepadConnected()) {

				// 各種感度の設定 //

				// 移動速度
				const float moveSpeed = 0.05f;

				// 回転速度
				const float rotateSpeed = 0.03f;

				// 拡大縮小速度
				const float scaleSpeed = 0.01f;

				Input::JoystickState leftStick = input->GetLeftStick();
				Input::JoystickState rightStick = input->GetRightStick();

				//===========================================
				// 【Translate】(位置移動)
				//===========================================

				// 左スティック X/Y : X軸(左右)・Y軸(上下)移動
				activeTransform->translate.x += leftStick.x * moveSpeed;
				activeTransform->translate.y += leftStick.y * moveSpeed;

				// RB / LB ボタン : Z軸(前後)移動

				// RB
				if (input->IsPressButton(XINPUT_GAMEPAD_RIGHT_SHOULDER)) {
					activeTransform->translate.z += moveSpeed;
				}

				// LB
				if (input->IsPressButton(XINPUT_GAMEPAD_LEFT_SHOULDER)) {
					activeTransform->translate.z -= moveSpeed;
				}

				//===========================================
				// 【Rotate】(回転)
				//===========================================

				// 右スティック X/Y : Y軸(左右回転)・X軸(上下ピッチ回転)
				activeTransform->rotate.y += rightStick.x * rotateSpeed;

				// 上下に傾ける操作感に合わせ反転
				activeTransform->rotate.x -= rightStick.y * rotateSpeed;

				// D-Pad(十字キー)左右 : Z軸(ロール回転)
				if (input->IsPressButton(XINPUT_GAMEPAD_DPAD_RIGHT)) {
					activeTransform->rotate.z -= rotateSpeed;
				}

				if (input->IsPressButton(XINPUT_GAMEPAD_DPAD_LEFT)) {
					activeTransform->rotate.z += rotateSpeed;
				}

				//===========================================
				// 【Scale】(拡大・縮小)
				//===========================================

				// D-Pad(十字キー)上/下 : 全軸均等スケール変更
				if (input->IsPressButton(XINPUT_GAMEPAD_DPAD_UP)) {
					activeTransform->scale.x += scaleSpeed;
					activeTransform->scale.y += scaleSpeed;
					activeTransform->scale.z += scaleSpeed;
				}

				if (input->IsPressButton(XINPUT_GAMEPAD_DPAD_DOWN)) {
					// スケールが 0 以下にならないよう制限
					activeTransform->scale.x = (std::max)(0.01f, activeTransform->scale.x - scaleSpeed);
					activeTransform->scale.y = (std::max)(0.01f, activeTransform->scale.y - scaleSpeed);
					activeTransform->scale.z = (std::max)(0.01f, activeTransform->scale.z - scaleSpeed);
				}
			}

			// === デバッグカメラの更新(行列計算の前に呼び出す) === //
			if (useDebugCamera) {
#ifdef USE_IMGUI
				// キーボードもマウスもImGuiが操作中でない時だけカメラを動かす
				ImGuiIO& imguiIO = ImGui::GetIO();
				if (!imguiIO.WantCaptureKeyboard && !imguiIO.WantCaptureMouse) {
					debugCamera->Update(input);
				}
#else
				debugCamera->Update(input);
#endif
			}

			//===================================
			// データの計算・定数バッファの更新
			//===================================

			// Sprite UV行列の作成と書き込み
			Matrix4x4 uvTransformMatrix = MathUtils::MakeScaleMatrix(uvTransformSprite.scale);
			uvTransformMatrix = MathUtils::Multiply(uvTransformMatrix, MathUtils::MakeRotateZMatrix(uvTransformSprite.rotate.z));
			uvTransformMatrix = MathUtils::Multiply(uvTransformMatrix, MathUtils::MakeTranslateMatrix(uvTransformSprite.translate));

			spriteMaterialData->uvTransform = uvTransformMatrix;

			// 3D View / Projection 行列の計算
			Matrix4x4 modelViewMatrix;
			Matrix4x4 modelProjectionMatrix;

			if (useDebugCamera) {
				modelViewMatrix = debugCamera->GetViewMatrix();
				modelProjectionMatrix = debugCamera->GetProjectionMatrix();
			} else {
				Matrix4x4 cameraMatrix = MathUtils::MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
				modelViewMatrix = MathUtils::Inverse(cameraMatrix);
				modelProjectionMatrix = MathUtils::MakePerspectiveFovMatrix(0.45f, static_cast<float>(kClientWidth) / static_cast<float>(kClientHeight), 0.1f, 100.0f);
			}

			// 3Dモデル(Plane / Sphere / Teapot / Bunny / MultiMesh / MultiMaterial)のWVP・World行列更新
			Matrix4x4* activeWvpData = planeModelWvpData;

			switch (currentModelIndex) {
			case 0:
				activeTransform = &planeModelTransform;
				activeWvpData = planeModelWvpData;
				break;
			case 1:
				activeTransform = &sphereTransform;
				activeWvpData = sphereWvpData;
				break;
			case 2:
				activeTransform = &teapotModelTransform;
				activeWvpData = teapotModelWvpData;
				break;
			case 3:
				activeTransform = &bunnyModelTransform;
				activeWvpData = bunnyModelWvpData;
				break;
			case 4:
				activeTransform = &multiMeshModelTransform;
				activeWvpData = multiMeshModelWvpData;
				break;
			case 5:
				activeTransform = &multiMaterialModelTransform;
				activeWvpData = multiMaterialModelWvpData;
				break;
			case 6:
				activeTransform = &suzanneModelTransform;
				activeWvpData = suzanneModelWvpData;
				break;
			}

			Matrix4x4 activeWorldMatrix = MathUtils::MakeAffineMatrix(activeTransform->scale, activeTransform->rotate, activeTransform->translate);
			Matrix4x4 matActiveWV = MathUtils::Multiply(activeWorldMatrix, modelViewMatrix);
			Matrix4x4 activeWvpMatrix = MathUtils::Multiply(matActiveWV, modelProjectionMatrix);

			activeWvpData[0] = activeWvpMatrix;
			activeWvpData[1] = activeWorldMatrix;

			// 2D Sprite のWVP・World行列更新
			Matrix4x4 spriteWorldMatrix = MathUtils::MakeAffineMatrix(spriteTransform.scale, spriteTransform.rotate, spriteTransform.translate);
			Matrix4x4 spriteViewMatrix = MathUtils::MakeIdentity4x4();
			Matrix4x4 spriteProjectionMatrix = MathUtils::MakeOrthographicMatrix(0.0f, 0.0f, static_cast<float>(kClientWidth), static_cast<float>(kClientHeight), 0.0f, 100.0f);
			Matrix4x4 spriteWvpMatrix = MathUtils::Multiply(spriteWorldMatrix, MathUtils::Multiply(spriteViewMatrix, spriteProjectionMatrix));

			spriteTransformMatrixData[0] = spriteWvpMatrix;
			spriteTransformMatrixData[1] = spriteWorldMatrix;

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
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

			// TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);
#pragma endregion

			// 描画先のRTVとDSVを設定する
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			dsvHandle.ptr += (0 * descriptorSizeDSV);

			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);

			// 青っぽい色。RGBAの順番で指定する
			float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };

			// 指定した色で画面全体をクリアする
			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);

			// 指定した深度で画面全体をクリアする
			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			// 描画用のDescriptorHeapの設定
			ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
			commandList->SetDescriptorHeaps(1, descriptorHeaps);

			// Viewportを設定する
			commandList->RSSetViewports(1, &viewport);

			// Scissorを設定する
			commandList->RSSetScissorRects(1, &scissorRect);

			// RootSignatureを設定する
			commandList->SetGraphicsRootSignature(rootSignature.Get());

			// PSOを設定する
			commandList->SetPipelineState(graphicsPipelineState.Get());

			// トポロジ(形状)を設定する
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			//==========================================
			// モデル(3D)の描画設定(モデル切り替えも対応)
			//==========================================

			// 共通のライト設定
			commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource.Get()->GetGPUVirtualAddress());

			// 選択されているモデルに応じて【マテリアル・WVP行列・頂点バッファ・テクスチャ・DrawCall】を切り替える
			if (currentModelIndex == 0) {
				// Plane.obj の描画設定
				commandList->SetGraphicsRootConstantBufferView(1, planeModelWvpResource.Get()->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(0, planeModelMaterialResource.Get()->GetGPUVirtualAddress());
				commandList->IASetVertexBuffers(0, 1, &planeModelVertexBufferView);
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU1);

				// Plane.obj の描画
				commandList->DrawInstanced(static_cast<UINT>(planeModelData.vertices.size()), 1, 0, 0);

			} else if (currentModelIndex == 1) {
				// Sphereの描画設定
				commandList->SetGraphicsRootConstantBufferView(0, sphereMaterialResource.Get()->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(1, sphereWvpResource.Get()->GetGPUVirtualAddress());
				commandList->IASetVertexBuffers(0, 1, &sphereVertexBufferView);

				// 球体用のIBVを設定
				commandList->IASetIndexBuffer(&sphereIndexBufferView);

				// uvChecker.png(GPU3)のテクスチャを設定
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU3);

				// インデックス(1536個)を使用して球体を描画
				commandList->DrawIndexedInstanced(sphereIndexCount, 1, 0, 0, 0);

			} else if (currentModelIndex == 2) {
				// Utah Teapot(teapot.obj)の描画設定
				commandList->SetGraphicsRootConstantBufferView(1, teapotModelWvpResource.Get()->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(0, teapotModelMaterialResource.Get()->GetGPUVirtualAddress());
				commandList->IASetVertexBuffers(0, 1, &teapotModelVertexBufferView);
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU4);

				// Teapot の描画
				commandList->DrawInstanced(static_cast<UINT>(teapotModelData.vertices.size()), 1, 0, 0);

			} else if (currentModelIndex == 3) {
				// Stanford Bunny(bunny.obj)の描画設定
				commandList->SetGraphicsRootConstantBufferView(1, bunnyModelWvpResource.Get()->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(0, bunnyModelMaterialResource.Get()->GetGPUVirtualAddress());
				commandList->IASetVertexBuffers(0, 1, &bunnyModelVertexBufferView);
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU5);

				// Bunny の描画
				commandList->DrawInstanced(static_cast<UINT>(bunnyModelData.vertices.size()), 1, 0, 0);

			} else if (currentModelIndex == 4) {
				// MultiMesh(multiMesh.obj)の描画設定
				commandList->SetGraphicsRootConstantBufferView(1, multiMeshModelWvpResource.Get()->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(0, multiMeshModelMaterialResource.Get()->GetGPUVirtualAddress());
				commandList->IASetVertexBuffers(0, 1, &multiMeshModelVertexBufferView);
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU6);

				// 全頂点を一括描画(分離せずに全オブジェクトを描画)
				commandList->DrawInstanced(static_cast<UINT>(multiMeshModelData.vertices.size()), 1, 0, 0);

			} else if (currentModelIndex == 5) {
				// MultiMaterial(multiMaterial.obj)の描画設定
				commandList->SetGraphicsRootConstantBufferView(1, multiMaterialModelWvpResource.Get()->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(0, multiMaterialModelMaterialResource.Get()->GetGPUVirtualAddress());
				commandList->IASetVertexBuffers(0, 1, &multiMaterialModelVertexBufferView);

				// materials 配列に2つ以上データがある場合は正しく分割して描画する
				if (multiMaterialModelData.materials.size() >= 2) {

					// 1つ目のオブジェクト(左)
					commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU3);
					commandList->DrawInstanced(multiMaterialModelData.materials[0].vertexCount, 1, multiMaterialModelData.materials[0].vertexStartIndex, 0);

					// 2つ目のオブジェクト(右)
					commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU8);
					commandList->DrawInstanced(multiMaterialModelData.materials[1].vertexCount, 1, multiMaterialModelData.materials[1].vertexStartIndex, 0);
				} else {
					// 分割数が足りない場合は全頂点を描画
					commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU3);
					commandList->DrawInstanced(static_cast<UINT>(multiMaterialModelData.vertices.size()), 1, 0, 0);
				}
			} else if (currentModelIndex == 6) {
				// Suzanne(suzanne.obj)の描画設定

				// 定数バッファ(WVP行列 & マテリアル)のセット
				commandList->SetGraphicsRootConstantBufferView(1, suzanneModelWvpResource.Get()->GetGPUVirtualAddress());
				commandList->SetGraphicsRootConstantBufferView(0, suzanneModelMaterialResource.Get()->GetGPUVirtualAddress());

				// 頂点バッファのセット
				commandList->IASetVertexBuffers(0, 1, &suzanneModelVertexBufferView);

				// テクスチャの設定(白テクスチャのSRVを指定してマテリアルカラーを素直に発色させる)
				// 白画像用のSRV(textureSrvHandleGPU1)を設定
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU1);

				// 描画(DrawCall)
				commandList->DrawInstanced(static_cast<UINT>(suzanneModelData.vertices.size()), 1, 0, 0);
			}

			//==========================================
			// Sprite(2D)の描画設定
			//==========================================

			if (isSpriteVisible) {

				// マテリアルの設定
				commandList->SetGraphicsRootConstantBufferView(0, spriteMaterialResource->GetGPUVirtualAddress());

				// Sprite用のVBVを設定(これで三角形のVBVが上書きされる)
				commandList->IASetVertexBuffers(0, 1, &spriteVertexBufferView);

				// IBVを設定
				commandList->IASetIndexBuffer(&spriteIndexBufferView);

				// Sprite用のWVP行列CBVを設定(これで三角形のCBVが上書きされる)
				commandList->SetGraphicsRootConstantBufferView(1, spriteTransformMatrixResource->GetGPUVirtualAddress());

				// テクスチャの設定
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU2);

				// Spriteの描画コマンド!!(DrawCall) 6個のインデックスを使用し、1つのインスタンスを描画
				commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
			}

			// 実際のcommandListのImGui描画コマンドを積む
#ifdef USE_IMGUI
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());
#endif

			// RenderTargetからPresentにする
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

			// TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

			// コマンドリストの内容を確定させる
			hr = commandList->Close();
			assert(SUCCEEDED(hr));
#pragma endregion

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

			// 次フレーム用のコマンドリストを準備する
			hr = commandAllocator->Reset();
			assert(SUCCEEDED(hr));

			hr = commandList->Reset(commandAllocator.Get(), nullptr);
			assert(SUCCEEDED(hr));
#pragma endregion
#pragma endregion

		}
	}
#pragma endregion

	// 終了時に記録する
	Log(logStream, "Game Engine Terminated.");

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

	//===========================
	// 入力管理クラスの解放処理
	//===========================

	if (input) {
		input->Finalize();
		delete input;
		input = nullptr;
	}

	// pSourceVoiceの解放
	if (pSourceVoice) {
		pSourceVoice->DestroyVoice();
		pSourceVoice = nullptr;
	}

	// 音声データの解放
	audioManager->SoundUnload(&soundData1);

	// XAudio2の解放処理
	audioManager->Finalize();

	// ウィンドウを閉じる
	CloseWindow(hwnd);

#pragma endregion

	// COMの終了処理
	CoUninitialize();

	return 0;
}
#pragma endregion