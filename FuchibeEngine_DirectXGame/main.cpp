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
	int32_t enableLighting;
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
};

// モデルデータ
struct ModelData {
	std::vector<VertexData> vertices;
	MaterialData material;
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
		} else if (identifier == "f") {

			VertexData triangle[3];

			// 面は三角形限定(その他は未対応)
			for (int32_t faceVertex = 0; faceVertex < 3; faceVertex++) {
				std::string vertexDefinition;
				s >> vertexDefinition;

				// 頂点の要素へのIndexは「位置/UV/法線」で格納されているため、分解してIndexを取得する
				std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3];

				for (int32_t element = 0; element < 3; element++) {
					std::string index;

					// 「/区切り」でIndexを読んでいく
					std::getline(v, index, '/');
					elementIndices[element] = std::stoi(index);
				}

				// 要素のIndexから実際の要素の値を取得して、頂点を構築する
				Vector4 position = positions[elementIndices[0] - 1];
				Vector2 texcoord = texcoords[elementIndices[1] - 1];
				Vector3 normal = normals[elementIndices[2] - 1];
				triangle[faceVertex] = { position, texcoord, normal };
				/*VertexData vertex = { position, texcoord, normal };
				modelData.vertices.push_back(vertex);*/
			}
			// 頂点を逆順で登録することで、回り順を逆にする
			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[0]);
		} else if (identifier == "mtllib") {

			// materialTemplateLibraryファイルの名前を取得する
			std::string materialFilename;
			s >> materialFilename;

			// 基本的にobjファイルと同一階層にmtlを存在させるため、ディレクトリ名とファイル名を渡す
			modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
		}
	}
	// ④modelDataを返す //
	return modelData;
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

	// ログ出力用のディレクトリを作成する
	std::filesystem::create_directory("logs");

#pragma endregion

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
	D3D12_ROOT_PARAMETER rootParameters[4] = {};

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

	// CBVを使う
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;

	// PixelShaderで使う
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// レジスタ番号1を使う
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

#pragma endregion

	//=======================
	// VertexResourceの生成
	//=======================

#pragma region VertexResourceの生成

//====================================
// 「plane.obj」のモデルとテクスチャ
//====================================

	// 「plane.obj」モデルの読み込み
	ModelData modelData1 = LoadObjFile("resources", "plane.obj");

	// 「plane.obj」モデルの頂点リソースの生成
	ID3D12Resource* vertexResource1 = CreateBufferResource(device, sizeof(VertexData) * modelData1.vertices.size());

	// Sprite用の頂点リソースを作成する
	ID3D12Resource* vertexResourceSprite = CreateBufferResource(device, sizeof(VertexData) * 6);

	//==========================================
	// 「axis.obj」のモデルとテクスチャ
	//==========================================

	ModelData modelData2 = LoadObjFile("resources", "axis.obj");
	ID3D12Resource* vertexResource2 = CreateBufferResource(device, sizeof(VertexData) * modelData2.vertices.size());

#pragma endregion

	//=========================
	// MaterialResourceの生成
	//=========================

#pragma region MaterialResourceの生成

	//===========================
	// 「plane.obj」のマテリアル
	//===========================

	// マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	ID3D12Resource* materialResource1 = CreateBufferResource(device, sizeof(Material));

	// マテリアルにデータを書き込む
	Material* materialData1 = nullptr;

	// 書き込むためのアドレスを取得する
	materialResource1->Map(0, nullptr, reinterpret_cast<void**>(&materialData1));

	// 構造体の各メンバにデータを代入する

	// 白色にする
	materialData1->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Lightingを有効にする
	materialData1->enableLighting = true;
	materialData1->uvTransform = MathUtils::MakeIdentity4x4();

	//===========================
	// 「axis.obj」のマテリアル
	//===========================

	// マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	ID3D12Resource* materialResource2 = CreateBufferResource(device, sizeof(Material));

	// マテリアルにデータを書き込む
	Material* materialData2 = nullptr;

	// 書き込むためのアドレスを取得する
	materialResource2->Map(0, nullptr, reinterpret_cast<void**>(&materialData2));

	// 構造体の各メンバにデータを代入する

	// 白色にする
	materialData2->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Lightingを有効にする
	materialData2->enableLighting = true;
	materialData2->uvTransform = MathUtils::MakeIdentity4x4();

#pragma endregion

	//================================
	// MaterialSpriteResourceの生成
	//================================

#pragma region MaterialSpriteResourceの生成

	// Sprite用のMaterialResourceを作る
	ID3D12Resource* materialResourceSprite = CreateBufferResource(device, sizeof(Material));

	// MaterialSpriteDataにデータを書き込む
	Material* materialSpriteData = nullptr;

	// 書き込むためのアドレスを取得する
	materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialSpriteData));

	// 構造体の各メンバにデータを代入する

	// 白色にする
	materialSpriteData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// SpriteはLightingしないのでfalseにする
	materialSpriteData->enableLighting = false;

	materialSpriteData->uvTransform = MathUtils::MakeIdentity4x4();

#pragma endregion

	//=================================
	// DirectionalLightResourceの生成
	//=================================

#pragma region DirectionalLightResourceの生成

	// DirectionalLight用のResourceを作る
	ID3D12Resource* directionalLightResource = CreateBufferResource(device, sizeof(DirectionalLight));

	// DirectionalLightResourceにデータを書き込む
	DirectionalLight* directionalLightData = nullptr;

	// 書き込むためのアドレスを取得する
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));

	// 構造体の各メンバにデータを代入する
	directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	directionalLightData->direction = { 0.0f, -1.0f, 0.0f };

	// 正規化する
	directionalLightData->direction = MathUtils::Normalize(directionalLightData->direction);
	directionalLightData->intensity = 1.0f;

#pragma endregion

	//===========================
	// IndexResourceSpriteの生成
	//===========================

#pragma region IndexResourceSpriteの生成

	ID3D12Resource* indexResourceSprite = CreateBufferResource(device, sizeof(uint32_t) * 6);

#pragma endregion

	//====================================
	// TransformMatrixResourceの生成
	//====================================

#pragma region TransformMatrixResourceの生成

	// SphereのWVP用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	ID3D12Resource* wvpResource = CreateBufferResource(device, sizeof(Matrix4x4) * 2);

	// データを書き込む
	Matrix4x4* wvpData = nullptr;

	// 書き込むためのアドレスを取得
	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));

	// HLSL側のwvpに単位行列を書き込む
	*wvpData = MathUtils::MakeIdentity4x4();

	// HLSL側のworldにも単位行列(または物体のワールド行列)を書き込む
	*(wvpData + 1) = MathUtils::MakeIdentity4x4();

	// Sprite用もWVP用と同様にMatrix4x4 2つ分 のサイズ(128バイト)を用意する
	ID3D12Resource* transformMatrixResourceSprite = CreateBufferResource(device, sizeof(Matrix4x4) * 2);

	// データを書き込む
	Matrix4x4* transformMatrixDataSprite = nullptr;

	// 書き込むためのアドレスを取得
	transformMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformMatrixDataSprite));

	// 1つ目の行列(wvp用)に単位行列を書き込む
	*transformMatrixDataSprite = MathUtils::MakeIdentity4x4();

	// 2つ目の行列(world用)にも単位行列を書き込む(+1 して次のアドレスへ)
	*(transformMatrixDataSprite + 1) = MathUtils::MakeIdentity4x4();

#pragma endregion

	//===========================
	// VertexBafferViewの設定
	//===========================

#pragma region VertexBafferViewの設定

	//=======================
	// 「plane.obj」モデル
	//=======================

	// 「plane.obj」モデルの頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView1{};

	// リソースの先頭アドレスから使う
	vertexBufferView1.BufferLocation = vertexResource1->GetGPUVirtualAddress();

	// VertexResourceで計算した正しいバイトサイズを設定する
	vertexBufferView1.SizeInBytes = UINT(sizeof(VertexData) * modelData1.vertices.size());

	// 1頂点あたりのサイズ
	vertexBufferView1.StrideInBytes = sizeof(VertexData);

	//=======================
	// 「axis.obj」モデル
	//=======================

	// 「axis.obj」モデルの頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView2{};

	// リソースの先頭アドレスから使う
	vertexBufferView2.BufferLocation = vertexResource2->GetGPUVirtualAddress();

	// VertexResourceで計算した正しいバイトサイズを設定する
	vertexBufferView2.SizeInBytes = UINT(sizeof(VertexData) * modelData2.vertices.size());

	// 1頂点あたりのサイズ
	vertexBufferView2.StrideInBytes = sizeof(VertexData);
#pragma endregion

	//================================
	// Resourceに頂点データを書きこむ
	//================================

#pragma region Resourceに頂点データを書きこむ

	//=======================
	// 「plane.obj」のモデル
	//=======================

	// 「plane.obj」モデルの頂点リソースにデータを書き込む
	VertexData* vertexData1 = nullptr;

	// 書き込むためのアドレスを取得する
	vertexResource1->Map(0, nullptr, reinterpret_cast<void**>(&vertexData1));

	// 頂点データをリソースにコピー
	std::memcpy(vertexData1, modelData1.vertices.data(), sizeof(VertexData) * modelData1.vertices.size());
	vertexResource1->Unmap(0, nullptr);

	//=======================
	// 「axis.obj」のモデル
	//=======================

	// 「axis.obj」モデルの頂点リソースにデータを書き込む
	VertexData* vertexData2 = nullptr;

	// 書き込むためのアドレスを取得する
	vertexResource2->Map(0, nullptr, reinterpret_cast<void**>(&vertexData2));

	// 頂点データをリソースにコピー
	std::memcpy(vertexData2, modelData2.vertices.data(), sizeof(VertexData) * modelData2.vertices.size());
	vertexResource2->Unmap(0, nullptr);

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

	// モデル用のTransformを作成する
	Transform transform{ {1.0f, 1.0f, 1.0f}, {0.0f, static_cast<float>(M_PI), 0.0f}, {0.0f, 0.0f, 0.0f} };

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

	//==========================================
	// 「plane.obj」のモデルのテクスチャを読み込む
	//==========================================

	DirectX::ScratchImage mipImages1 = LoadTexture(modelData1.material.textureFilePath);
	const DirectX::TexMetadata& metadata1 = mipImages1.GetMetadata();

	// リソース作成
	ID3D12Resource* textureResource1 = CreateTextureResource(device, metadata1);

	//===========================================
	// 「axis.obj」のモデルのテクスチャを読み込む
	//===========================================

	DirectX::ScratchImage mipImages2 = LoadTexture(modelData2.material.textureFilePath);
	const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();

	// リソース作成
	ID3D12Resource* textureResource2 = CreateTextureResource(device, metadata2);

#pragma endregion

	//===========================
	// コマンドを実行して完了を待つ
	//===========================

#pragma region コマンドを実行して完了を待つ

	//===============================
	// 「plane.obj」のテクスチャを転送
	//===============================

	// 転送関数を呼び出し、コピーコマンドをコマンドリストに積む(中間リソースが戻る)
	ID3D12Resource* intermediateResource1 = UploadTextureData(textureResource1, mipImages1, device, commandList);

	//===============================
	// 「axis.obj」のテクスチャを転送
	//===============================

	// 転送関数を呼び出し、コピーコマンドをコマンドリストに積む(中間リソースが戻る)
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
	intermediateResource1->Release();
	mipImages1.Release();
	intermediateResource2->Release();
	mipImages2.Release();

#pragma endregion

#pragma endregion

	//==========================
	// SRVDescriptorHeapの生成
	//==========================

#pragma region SRVDescriptorHeapの生成

	//===========================================
	// 「plane.obj」のテクスチャのSRVを作成
	//===========================================

	// metaDataを基にSRVを作成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc1{};
	srvDesc1.Format = metadata1.format;
	srvDesc1.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// 2Dテクスチャ
	srvDesc1.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

	// テクスチャリソースが持っているすべてのミップマップレベルを自動的にすべて割り当てる(符号なし整数の最大値を直接表す「0xFFFFFFFF」に書き換える)
	srvDesc1.Texture2D.MipLevels = 0xFFFFFFFF;

	// SRVを作成するDescriptorHeapの場所を決める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU1 = srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU1 = srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

	// 先頭はImGuiが使っているのでその次を使う
	textureSrvHandleCPU1.ptr += descriptorSizeSRV;
	textureSrvHandleGPU1.ptr += descriptorSizeSRV;

	// SRVを作成(インデックス1の場所に書き込まれる)
	device->CreateShaderResourceView(textureResource1, &srvDesc1, textureSrvHandleCPU1);

	//===========================================
	// 「axis.obj」のテクスチャのSRVを作成
	//===========================================

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};
	srvDesc2.Format = metadata2.format;
	srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// 2Dテクスチャ
	srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc2.Texture2D.MipLevels = 0xFFFFFFFF;

	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = textureSrvHandleCPU1;
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = textureSrvHandleGPU1;

	textureSrvHandleCPU2.ptr += descriptorSizeSRV;
	textureSrvHandleGPU2.ptr += descriptorSizeSRV;

	// SRVを作成(インデックス2の場所に書き込まれる)
	device->CreateShaderResourceView(textureResource2, &srvDesc2, textureSrvHandleCPU2);

#pragma endregion

	//============================
	// スプライトの位置を保持する変数
	//============================

#pragma region スプライトの位置を保持する変数

	// スプライトの位置(X, Y)初期値は(0, 0)
	float spritePos[2] = { 0.0f, 0.0f };

	// カメラのTransform(Zの初期値を -10.0f に設定)
	Transform cameraTransform{ {1.0f, 1.0f, 1.0f},{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -10.0f} };

#pragma endregion

	//=======================
	// ImGuiの切り替え用の変数
	//=======================

	// 0: plane.obj, 1: axis.obj
	int currentModelIndex = 0;

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

			// モデルのトランスフォーム操作UI
			ImGui::Text("-Model (plane.obj) Transform-");

			// 各軸の回転を操作するスライダー
			ImGui::SliderFloat3("Model Rotation", &transform.rotate.x, -static_cast<float>(M_PI), static_cast<float>(M_PI));

			ImGui::DragFloat3("Model Scale", &transform.scale.x, 0.1f);

			// リセット時の値をY軸180度にする
			if (ImGui::Button("Reset Rotation")) {
				// 完全な0ではなく、正面を向く Y軸180度(M_PI)にリセットする
				transform.rotate = { 0.0f, static_cast<float>(M_PI), 0.0f };
			}

			ImGui::Separator();

			// SpriteのUV座標系を動かせる
			ImGui::Text("UV Transform");
			ImGui::DragFloat2("UV Translate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
			ImGui::DragFloat2("UV Scale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
			ImGui::SliderAngle("UV Rotate", &uvTransformSprite.rotate.z);

			ImGui::Separator();

			// モデルの切り替えラジオボタン
			ImGui::Text("-Select Model-");
			ImGui::RadioButton("Plane (plane.obj)", &currentModelIndex, 0);
			ImGui::RadioButton("Axis (axis.obj)", &currentModelIndex, 1);

			ImGui::Separator();

			// 色編集用のImGui
			ImGui::ColorEdit3("Sphere Color", &materialData1->color.x);

			ImGui::Separator();

			// ライトの設定
			ImGui::Text("Directional Light");

			// ライトの色変更
			ImGui::ColorEdit3("Light Color", &directionalLightData->color.x);

			// ライトの輝度(0.0 ~ 10.0 程度まで動かせるように設定)
			ImGui::SliderFloat("Intensity", &directionalLightData->intensity, 0.0f, 10.0f);

			// ライトの向き(-10.0 ~ 10.0 の範囲で動かす)
			if (ImGui::SliderFloat3("Light Direction", &directionalLightData->direction.x, -10.0f, 10.0f)) {

				// 値が変わったら毎回正規化する
				directionalLightData->direction = MathUtils::Normalize(directionalLightData->direction);
			}

			ImGui::End();
#endif

			// === データの計算・定数バッファの更新 === //

			// WorldMatrixを作成
			Matrix4x4 worldMatrix = MathUtils::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
			Matrix4x4 cameraMatrix = MathUtils::MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
			Matrix4x4 viewMatrix = MathUtils::Inverse(cameraMatrix);
			Matrix4x4 projectionMatrix = MathUtils::MakePerspectiveFovMatrix(0.45f, static_cast<float>(kClientWidth) / static_cast<float>(kClientHeight), 0.1f, 100.0f);

			// wvpMatrixを作成して更新
			Matrix4x4 matWV = MathUtils::Multiply(worldMatrix, viewMatrix);
			Matrix4x4 worldViewProjectionMatrix = MathUtils::Multiply(matWV, projectionMatrix);

			// CBufferの中身を更新
			*wvpData = worldViewProjectionMatrix;

			// UVTransform行列の作成と更新
			Matrix4x4 uvTransformMatrix = MathUtils::MakeScaleMatrix(uvTransformSprite.scale);
			uvTransformMatrix = MathUtils::Multiply(uvTransformMatrix, MathUtils::MakeRotateZMatrix(uvTransformSprite.rotate.z));
			uvTransformMatrix = MathUtils::Multiply(uvTransformMatrix, MathUtils::MakeTranslateMatrix(uvTransformSprite.translate));

			if (currentModelIndex == 0) {
				materialData1->uvTransform = uvTransformMatrix;
			} else {
				materialData2->uvTransform = uvTransformMatrix;
			}

			// 描画用の頂点数を決定
			UINT activeVertexCount = (currentModelIndex == 0) ? UINT(modelData1.vertices.size()) : UINT(modelData2.vertices.size());

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

			// トポロジ(形状)を設定する。PSOに設定しているものとはまた別。同じものを設定すると考えておくといい
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			// 常に1枚目のテクスチャ(textureSrvHandleGPU1)をバインドする
			commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU1);
			
			//==========================================
			// モデル(3D)の描画設定(モデル切り替えも対応)
			//==========================================

			// 共通のライト設定
			commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());
			
			// 共通のWVP行列設定
			commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());

			// 選択されているモデルに応じて【マテリアル・頂点バッファ・テクスチャ】をすべて正しく切り替える
			if (currentModelIndex == 0) {

				// Plane の描画設定
				commandList->SetGraphicsRootConstantBufferView(0, materialResource1->GetGPUVirtualAddress());
				commandList->IASetVertexBuffers(0, 1, &vertexBufferView1);
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU1);
			} else {
				// Axisの描画設定
				commandList->SetGraphicsRootConstantBufferView(0, materialResource2->GetGPUVirtualAddress());
				commandList->IASetVertexBuffers(0, 1, &vertexBufferView2);
				commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU2);
			}

			// 確定した正しい頂点数(activeVertexCount)で、この1回だけ描画する
			commandList->DrawInstanced(activeVertexCount, 1, 0, 0);

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
	if (fenceEvent != nullptr) { CloseHandle(fenceEvent); }
	if (fence) { fence->Release(); }

	// パイプライン・シェーダー関連リソースの解放
	if (graphicsPipelineState) { graphicsPipelineState->Release(); }
	if (rootSignature) { rootSignature->Release(); }

	if (signatureBlob) { signatureBlob->Release(); }

	if (errorBlob) { errorBlob->Release(); }

	if (pixelShaderBlob) { pixelShaderBlob->Release(); }

	if (vertexShaderBlob) { vertexShaderBlob->Release(); }

	// バッファ・マテリアルリソースの解放
	if (wvpResource) { wvpResource->Release(); }
	if (materialResource1) { materialResource1->Release(); }
	if (materialResourceSprite) { materialResourceSprite->Release(); }
	if (directionalLightResource) { directionalLightResource->Release(); }
	if (vertexResource1) { vertexResource1->Release(); }
	if (vertexResource2) { vertexResource2->Release(); }
	if (indexResourceSprite) { indexResourceSprite->Release(); }
	if (vertexResourceSprite) { vertexResourceSprite->Release(); }
	if (transformMatrixResourceSprite) { transformMatrixResourceSprite->Release(); }
	if (textureResource1) { textureResource1->Release(); }
	if (textureResource2) { textureResource2->Release(); }

	// ディスクリプタヒープの解放
	if (srvDescriptorHeap) { srvDescriptorHeap->Release(); }
	if (rtvDescriptorHeap) { rtvDescriptorHeap->Release(); }
	if (dsvDescriptorHeap) { dsvDescriptorHeap->Release(); }

	// スワップチェーン関連の解放
	if (swapChainResources[0]) { swapChainResources[0]->Release(); }
	if (swapChainResources[1]) { swapChainResources[1]->Release(); }
	if (swapChain) { swapChain->Release(); }

	// コマンド関連・デバイスの解放
	if (commandList) { commandList->Release(); }
	if (commandAllocator) { commandAllocator->Release(); }
	if (commandQueue) { commandQueue->Release(); }

	if (useAdapter) { useAdapter->Release(); }
	if (dxgiFactory) { dxgiFactory->Release(); }

#ifdef _DEBUG
	if (debugController) { debugController->Release(); }
#endif

	// デバイスのReleaseが全て終わった後にデバイス本体を解放する
	if (device) { device->Release(); }

	// ウィンドウを閉じる
	CloseWindow(hwnd);

#pragma endregion

	// COMの終了処理
	CoUninitialize();

	return 0;
}

#pragma endregion