#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cassert>
#include <dbgHelp.h>
#include <strsafe.h>
#include <dxgidebug.h>
#include <dxcapi.h>
#include "MathUtils.h"
#include "externals/DirectXTex/DirectXTex.h"
#include "externals/DirectXTex/d3dx12.h"
#include <vector>
#include "Audio.h"
#include "Input.h"
#include "WindowsApi.h"
#include "DirectXPlatform.h"
#include "Logger.h"
#include "DebugCamera.h"
#include "D3DResourceLeakChecker.h"
#include "ParticleManager.h"
#include <random>
#include <memory>
#include <numbers>
#ifdef USE_IMGUI
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#endif
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "dxcompiler.lib")

//=======================
// 関数群
//=======================

#pragma region 関数群

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

	D3DResourceLeakChecker leakCheck;

	//=====================
	// ウィンドウの生成
	//=====================

	// ポインタ
	WindowsApi* windowsApi = nullptr;

	// WindowsApiの初期化
	windowsApi = new WindowsApi();
	windowsApi->Initialize();

	// ポインタ
	DirectXPlatform* dxPlatform = nullptr;

	// DirectXの初期化
	dxPlatform = new DirectXPlatform();
	dxPlatform->Initialize(windowsApi);

	ID3D12Device* device = dxPlatform->GetDevice();
	ID3D12GraphicsCommandList* commandList = dxPlatform->GetCommandList();

	//======================================
	// 入力管理クラスのインスタンス生成と初期化
	//======================================

	// Inputクラスのポインタを変数として宣言する
	Input* input = new Input();

	// 作成したインスタンスの初期化関数を呼び出す
	input->Initialize(windowsApi);

	//==============================
	// 現在時刻でのログファイルの作成
	//==============================

#pragma region 現在時刻でのログファイルの作成

	// 現在時刻を取得(UTC時刻)
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
	Logger::Log(logStream, "Game Engine Started.");

#pragma endregion

	//================================================================
	// RootSignatureとDescriptorRangeとRootParameterの生成
	//================================================================

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

	D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1] = {};

	// 0から始まる
	descriptorRangeForInstancing[0].BaseShaderRegister = 0;

	// 数は1つ
	descriptorRangeForInstancing[0].NumDescriptors = 1;

	// SRVを使う
	descriptorRangeForInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;

	// Offsetを自動で計算
	descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// RootParameterの生成
	// PixelShaderのMaterialとVertexShaderのTransformとSRV
	D3D12_ROOT_PARAMETER rootParameters[4] = {};

	// CBVを使う
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;

	// PixelShaderを使う
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// レジスタ番号0とバインドする
	rootParameters[0].Descriptor.ShaderRegister = 0;

	// DescriptorTableを使う
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;

	// VertexShaderで使う
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	// Tableの中身の配列を指定
	rootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRangeForInstancing;

	// Tableで利用する数
	rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeForInstancing);

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
	HRESULT hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);

	if (FAILED(hr)) {
		if (errorBlob) {
			Logger::Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
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

	//============================
	// BlendStateの設定
	//============================

#pragma region BlendStateの設定

	//==============
	// ブレンドなし
	//==============

#pragma region ブレンドなし

	D3D12_BLEND_DESC noneBlendDesc{};

	// 全ての色要素を書きこむ
	noneBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

#pragma endregion

	//==============
	// 通常ブレンド
	//==============

#pragma region 通常ブレンド

	D3D12_BLEND_DESC normalBlendDesc{};

	// 全ての色要素を書きこむ
	normalBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	normalBlendDesc.RenderTarget[0].BlendEnable = true;
	normalBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	normalBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	normalBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	normalBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	normalBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	normalBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

#pragma endregion

	//==============
	// 加算ブレンド
	//==============

#pragma region 加算ブレンド

	D3D12_BLEND_DESC addBlendDesc{};

	// 全ての色要素を書きこむ
	addBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	addBlendDesc.RenderTarget[0].BlendEnable = true;
	addBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	addBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	addBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	addBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	addBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	addBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

#pragma endregion

	//==============
	// 減算ブレンド
	//==============

#pragma region 減算ブレンド

	D3D12_BLEND_DESC subtractBlendDesc{};

	// 全ての色要素を書きこむ
	subtractBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	subtractBlendDesc.RenderTarget[0].BlendEnable = true;
	subtractBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	subtractBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
	subtractBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	subtractBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	subtractBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	subtractBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

#pragma endregion

	//================
	// 乗算ブレンド
	//================

#pragma region 乗算ブレンド

	D3D12_BLEND_DESC multiplyBlendDesc{};

	// 全ての色要素を書きこむ
	multiplyBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	multiplyBlendDesc.RenderTarget[0].BlendEnable = true;
	multiplyBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
	multiplyBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	multiplyBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
	multiplyBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	multiplyBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	multiplyBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

#pragma endregion

	//==================
	// スクリーンブレンド
	//==================

#pragma region スクリーンブレンド

	D3D12_BLEND_DESC screenBlendDesc{};

	// 全ての色要素を書きこむ
	screenBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	screenBlendDesc.RenderTarget[0].BlendEnable = true;
	screenBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
	screenBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	screenBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	screenBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	screenBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	screenBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

#pragma endregion

	// BlendModeの種類
	enum BlendMode {
		// ブレンドなし
		kBlendModeNone,
		// 通常αブレンド(デフォルト) Src * SrcA + Dest * (1 - SrcA)
		kBlendModeNormal,
		// 加算ブレンド Src * SrcA + Dest * 1
		kBlendModeAdd,
		// 減算ブレンド Dest * 1 - Src * SrcA
		kBlendModeSubtract,
		// 乗算ブレンド Src * 0 + Dest * Src
		kBlendModeMultiply,
		// スクリーンブレンド Src * (1 - Dest) + Dest * 1
		kBlendModeScreen,
		// 利用してはいけない
		kCountOfBlendMode
	};

	// BlendModeの要素順に対応する BlendDesc の配列
	D3D12_BLEND_DESC blendDescs[kCountOfBlendMode] = {
		// kBlendModeNone
		noneBlendDesc,
		// kBlendModeNormal
		normalBlendDesc,
		// kBlendModeAdd
		addBlendDesc,
		// kBlendModeSubtract
		subtractBlendDesc,
		// kBlendModeMultiply
		multiplyBlendDesc,
		// kBlendModeScreen
		screenBlendDesc
	};

	// 現在選択中のブレンドモード(初期値はブレンドなし: kBlendModeNone)
	static int currentBlendMode = kBlendModeNone;

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

	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = dxPlatform->CompileShader(L"Object3d.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = dxPlatform->CompileShader(L"Object3d.PS.hlsl", L"ps_6_0");
	assert(pixelShaderBlob != nullptr);

#pragma endregion

	//==========================
	// DepthStencilStateの設定
	//==========================

#pragma region DepthStencilStateの設定

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};

	// Depthの機能を有効化する
	depthStencilDesc.DepthEnable = true;

	// Depthの書き込みを無効化する。つまり、描画はされるが奥行きバッファには書き込まれない
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

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

	// 全ブレンドモードに対応するPSOを作成・保持する配列
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStates[kCountOfBlendMode] = {};

	for (size_t i = 0; i < kCountOfBlendMode; i++) {

		// 各ブレンドモードを設定
		graphicsPilelineStateDesc.BlendState = blendDescs[i];

		// 実際に生成する
		hr = device->CreateGraphicsPipelineState(&graphicsPilelineStateDesc, IID_PPV_ARGS(&graphicsPipelineStates[i]));
		assert(SUCCEEDED(hr));
	}

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

	//=====================================================
	// VertexResourceの生成
	//=====================================================

#pragma region VertexResourceの生成

//================================
// plane.objモデルとテクスチャ
//================================

#pragma region plane.objモデルとテクスチャ

	// plane.objモデルの読み込み
	ModelData planeModelData = LoadObjFile("resources", "plane.obj");

	// plane.objのテクスチャを circle.png に差し替える
	planeModelData.material.textureFilePath = "resources/circle.png";

	// plane.objモデルの頂点リソースの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> planeModelVertexResource = dxPlatform->CreateBufferResource(sizeof(VertexData) * planeModelData.vertices.size());

#pragma endregion

	//================================
	// axis.objモデルとテクスチャ
	//================================

#pragma region axis.objモデルとテクスチャ

	ModelData axisModelData = LoadObjFile("resources", "axis.obj");
	Microsoft::WRL::ComPtr<ID3D12Resource> axisModelVertexResource = dxPlatform->CreateBufferResource(sizeof(VertexData) * axisModelData.vertices.size());

#pragma endregion

	//================================
	// fence.objモデルとテクスチャ
	//================================

#pragma region fence.objモデルとテクスチャ

	ModelData fenceModelData = LoadObjFile("resources", "fence.obj");
	Microsoft::WRL::ComPtr<ID3D12Resource> fenceModelVertexResource = dxPlatform->CreateBufferResource(sizeof(VertexData) * fenceModelData.vertices.size());

#pragma endregion

#pragma endregion

	//=========================
	// MaterialResourceの生成
	//=========================

#pragma region MaterialResourceの生成

	//============================
	// plane.objモデルのマテリアル
	//============================

#pragma region plane.objモデルのマテリアル

	// マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> planeModelMaterialResource = dxPlatform->CreateBufferResource(sizeof(Material));

	// マテリアルにデータを書き込む
	Material* planeModelMaterialData = nullptr;

	// 書き込むためのアドレスを取得する
	planeModelMaterialResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&planeModelMaterialData));

	// 構造体の各メンバにデータを代入する

	// 白色にする
	planeModelMaterialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Lightingを有効にする
	planeModelMaterialData->enableLighting = true;
	planeModelMaterialData->uvTransform = MathUtils::MakeIdentity4x4();

#pragma endregion

	//===========================
	// axis.objモデルのマテリアル
	//===========================

#pragma region axis.objモデルのマテリアル

	// マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> axisModelMaterialResource = dxPlatform->CreateBufferResource(sizeof(Material));

	// マテリアルにデータを書き込む
	Material* axisModelMaterialData = nullptr;

	// 書き込むためのアドレスを取得する
	axisModelMaterialResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&axisModelMaterialData));

	// 構造体の各メンバにデータを代入する

	// 白色にする
	axisModelMaterialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Lightingを有効にする
	axisModelMaterialData->enableLighting = true;
	axisModelMaterialData->uvTransform = MathUtils::MakeIdentity4x4();

#pragma endregion

	//===========================
	// fence.objモデルのマテリアル
	//===========================

#pragma region fence.objモデルのマテリアル

	// マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> fenceModelMaterialResource = dxPlatform->CreateBufferResource(sizeof(Material));

	// マテリアルにデータを書き込む
	Material* fenceModelMaterialData = nullptr;

	// 書き込むためのアドレスを取得する
	fenceModelMaterialResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&fenceModelMaterialData));

	// 構造体の各メンバにデータを代入する

	// 白色にする
	fenceModelMaterialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Lightingを有効にする
	fenceModelMaterialData->enableLighting = true;
	fenceModelMaterialData->uvTransform = MathUtils::MakeIdentity4x4();

#pragma endregion

#pragma endregion

	//=================================
	// DirectionalLightResourceの生成
	//=================================

#pragma region DirectionalLightResourceの生成

	// DirectionalLight用のResourceを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource = dxPlatform->CreateBufferResource(sizeof(DirectionalLight));

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

	//====================================
	// TransformMatrixResourceの生成
	//====================================

#pragma region TransformMatrixResourceの生成

	// モデルのWVP用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource = dxPlatform->CreateBufferResource(sizeof(Matrix4x4) * 2);

	// データを書き込む
	Matrix4x4* wvpData = nullptr;

	// 書き込むためのアドレスを取得
	wvpResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));

	// HLSL側のwvpに単位行列を書き込む
	*wvpData = MathUtils::MakeIdentity4x4();

	// HLSL側のworldにも単位行列(または物体のワールド行列)を書き込む
	*(wvpData + 1) = MathUtils::MakeIdentity4x4();

#pragma endregion

	//===========================
	// VertexBafferViewの設定
	//===========================

#pragma region VertexBafferViewの設定

	//================厅
	// plane.objモデル
	//=======================

#pragma region plane.objモデル

	// plane.objモデルの頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW planeModelVertexBufferView{};

	// リソースの先頭アドレスから使う
	planeModelVertexBufferView.BufferLocation = planeModelVertexResource.Get()->GetGPUVirtualAddress();

	// VertexResourceで計算した正しいバイトサイズを設定する
	planeModelVertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * planeModelData.vertices.size());

	// 1頂点あたりのサイズ
	planeModelVertexBufferView.StrideInBytes = sizeof(VertexData);

#pragma endregion

	//=======================
	// axis.objモデル
	//=======================

#pragma region axis.objモデル

	// axis.objモデルの頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW axisModelVertexBufferView{};

	// リソースの先頭アドレスから使う
	axisModelVertexBufferView.BufferLocation = axisModelVertexResource.Get()->GetGPUVirtualAddress();

	// VertexResourceで計算した正しいバイトサイズを設定する
	axisModelVertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * axisModelData.vertices.size());

	// 1頂点あたりのサイズ
	axisModelVertexBufferView.StrideInBytes = sizeof(VertexData);

#pragma endregion

	//======================
	// fence.objモデル
	//======================

#pragma region fence.objモデル

	// fence.objモデルの頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW fenceModelVertexBufferView{};

	// リソースの先頭アドレスから使う
	fenceModelVertexBufferView.BufferLocation = fenceModelVertexResource.Get()->GetGPUVirtualAddress();

	// VertexResourceで計算した正しいバイトサイズを設定する
	fenceModelVertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * fenceModelData.vertices.size());

	// 1頂点あたりのサイズ
	fenceModelVertexBufferView.StrideInBytes = sizeof(VertexData);

#pragma endregion

#pragma endregion

	//================================
	// Resourceに頂点データを書きこむ
	//================================

#pragma region Resourceに頂点データを書きこむ

	//=======================
	// plane.objモデル
	//=======================

#pragma region plane.objモデル

	// plane.objモデルの頂点リソースにデータを書き込む
	VertexData* planeModelVertexData = nullptr;

	// 書き込むためのアドレスを取得する
	planeModelVertexResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&planeModelVertexData));

	// 頂点データをリソースにコピー
	std::memcpy(planeModelVertexData, planeModelData.vertices.data(), sizeof(VertexData) * planeModelData.vertices.size());
	planeModelVertexResource.Get()->Unmap(0, nullptr);

#pragma endregion

	//=======================
	// axis.objモデル
	//=======================

#pragma region axis.objモデル

	// axis.objモデルの頂点リソースにデータを書き込む
	VertexData* axisModelVertexData = nullptr;

	// 書き込むためのアドレスを取得する
	axisModelVertexResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&axisModelVertexData));

	// 頂点データをリソースにコピー
	std::memcpy(axisModelVertexData, axisModelData.vertices.data(), sizeof(VertexData) * axisModelData.vertices.size());
	axisModelVertexResource.Get()->Unmap(0, nullptr);

#pragma endregion

	//=======================
	// fence.objモデル
	//=======================

#pragma region fence.objモデル

	// fence.objモデルの頂点リソースにデータを書き込む
	VertexData* fenceModelVertexData = nullptr;

	// 書き込むためのアドレスを取得する
	fenceModelVertexResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&fenceModelVertexData));

	// 頂点データをリソースにコピー
	std::memcpy(fenceModelVertexData, fenceModelData.vertices.data(), sizeof(VertexData) * fenceModelData.vertices.size());
	fenceModelVertexResource.Get()->Unmap(0, nullptr);

#pragma endregion

#pragma endregion

	//================-------
	// Transform変数の作成
	//================-------

#pragma region Transform変数の作成

	// モデル用のTransformを作成する
	Transform planeModelTransform{ { 1.0f, 1.0f, 1.0f },{ 0.0f, std::numbers::pi_v<float>, 0.0f },{ 0.0f, 0.0f, 0.0f } };
	Transform axisModelTransform{ { 1.0f, 1.0f, 1.0f },{ 0.0f, std::numbers::pi_v<float>, 0.0f },{ 0.0f, 0.0f, 0.0f } };
	Transform fenceModelTransform{ { 1.0f, 1.0f, 1.0f },{ 0.0f, std::numbers::pi_v<float>, 0.0f },{ 0.0f, 0.0f, 0.0f } };

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
	// plane.objモデルのテクスチャを読み込む
	//==========================================

#pragma region plane.objモデル

	DirectX::ScratchImage planeModelMipImages = DirectXPlatform::LoadTexture(planeModelData.material.textureFilePath);
	const DirectX::TexMetadata& planeModelMetadata = planeModelMipImages.GetMetadata();

	// リソース作成
	Microsoft::WRL::ComPtr<ID3D12Resource> planeModelTextureResource = dxPlatform->CreateTextureResource(planeModelMetadata);

#pragma endregion

	//===========================================
	// axis.objモデルのテクスチャを読み込む
	//===========================================

#pragma region axis.objモデル

	DirectX::ScratchImage axisModelMipImages = DirectXPlatform::LoadTexture(axisModelData.material.textureFilePath);
	const DirectX::TexMetadata& axisModelMetadata = axisModelMipImages.GetMetadata();

	// リソース作成
	Microsoft::WRL::ComPtr<ID3D12Resource> axisModelTextureResource = dxPlatform->CreateTextureResource(axisModelMetadata);

#pragma endregion

	//===========================================
	// fence.objモデルのテクスチャを読み込む
	//===========================================

#pragma region fence.objモデル

	DirectX::ScratchImage fenceModelMipImages = DirectXPlatform::LoadTexture(fenceModelData.material.textureFilePath);
	const DirectX::TexMetadata& fenceModelMetadata = fenceModelMipImages.GetMetadata();

	// リソース作成
	Microsoft::WRL::ComPtr<ID3D12Resource> fenceModelTextureResource = dxPlatform->CreateTextureResource(fenceModelMetadata);

#pragma endregion

#pragma endregion

	//===========================
	// コマンドを実行して完了を待つ
	//===========================

#pragma region コマンドを実行して完了を待つ

	//=================================
	// plane.objモデルのテクスチャを転送
	//=================================

#pragma region plane.objモデルのテクスチャを転送

	// 転送関数を呼び出し、コピーコマンドをコマンドリストに積む(中間リソースが戻る)
	Microsoft::WRL::ComPtr<ID3D12Resource> planeModelIntermediateResource = dxPlatform->UploadTextureData(planeModelTextureResource, planeModelMipImages);

#pragma endregion

	//===============================
	// axis.objモデルのテクスチャを転送
	//===============================

#pragma region axis.objモデルのテクスチャを転送

	// 転送関数を呼び出し、コピーコマンドをコマンドリストに積む(中間リソースが戻る)
	Microsoft::WRL::ComPtr<ID3D12Resource> axisModelIntermediateResource = dxPlatform->UploadTextureData(axisModelTextureResource, axisModelMipImages);

#pragma endregion

	//================================
	// fence.objモデルのテクスチャを転送
	//================================

#pragma region fence.objモデルのテクスチャを転送

	// 転送関数を呼び出し、コピーコマンドをコマンドリストに積む(中間リソースが戻る)
	Microsoft::WRL::ComPtr<ID3D12Resource> fenceModelIntermediateResource = dxPlatform->UploadTextureData(fenceModelTextureResource, fenceModelMipImages);

#pragma endregion

	// 初期化時のテクスチャ転送コマンドの実行とGPU完了待ち
	dxPlatform->PreDraw();
	dxPlatform->PostDraw();

#pragma endregion

	//==========================
	// SRVDescriptorHeapの生成
	//==========================

#pragma region SRVDescriptorHeapの生成

	//===========================================
	// plane.objモデルのテクスチャのSRVを作成
	//===========================================

#pragma region plane.objモデルのテクスチャのSRVを作成

	// metaDataを基にSRVを作成
	D3D12_SHADER_RESOURCE_VIEW_DESC planeModelSrvDesc{};
	planeModelSrvDesc.Format = planeModelMetadata.format;
	planeModelSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// 2Dテクスチャ
	planeModelSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

	// テクスチャリソースが持っているすべてのミップマップレベルを自動的にすべて割り当てる(符号なし整数の最大値を直接表す「0xFFFFFFFF」に書き換える)
	planeModelSrvDesc.Texture2D.MipLevels = 0xFFFFFFFF;

	// SRVを作成(インデックス1の場所に書き込まれる)
	device->CreateShaderResourceView(planeModelTextureResource.Get(), &planeModelSrvDesc, dxPlatform->GetSRVCPUDescriptorHandle(1));

#pragma endregion

	//===========================================
	// axis.objモデルのテクスチャのSRVを作成
	//===========================================

#pragma region axis.objモデルのテクスチャのSRVを作成

	D3D12_SHADER_RESOURCE_VIEW_DESC axisModelSrvDesc{};
	axisModelSrvDesc.Format = axisModelMetadata.format;
	axisModelSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// 2Dテクスチャ
	axisModelSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	axisModelSrvDesc.Texture2D.MipLevels = 0xFFFFFFFF;

	// SRVを作成(インデックス2の場所に書き込まれる)
	device->CreateShaderResourceView(axisModelTextureResource.Get(), &axisModelSrvDesc, dxPlatform->GetSRVCPUDescriptorHandle(2));

#pragma endregion

	//===========================================
	// fence.objモデルのテクスチャのSRVを作成
	//===========================================

#pragma region fence.objモデルのテクスチャのSRVを作成

	D3D12_SHADER_RESOURCE_VIEW_DESC fenceModelSrvDesc{};
	fenceModelSrvDesc.Format = fenceModelMetadata.format;
	fenceModelSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	// 2Dテクスチャ
	fenceModelSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	fenceModelSrvDesc.Texture2D.MipLevels = 0xFFFFFFFF;

	// SRVを作成(インデックス3の場所に書き込まれる)
	device->CreateShaderResourceView(fenceModelTextureResource.Get(), &fenceModelSrvDesc, dxPlatform->GetSRVCPUDescriptorHandle(3));

#pragma endregion

#pragma endregion

	// パーティクルマネージャーの生成と初期化(インデックス4を使用)
	std::unique_ptr<ParticleManager> particleManager = std::make_unique<ParticleManager>();
	particleManager->Initialize(dxPlatform, 4);

	// DeltaTimeを定義する(ひとまず60fps固定)
	const float kDeltaTime = 1.0f / 60.0f;

	//================
	// サウンドの再生
	//================

#pragma region サウンドの再生

	audioManager->SoundPlayWave(soundData1);

#pragma endregion

	//============================
	// カメラのTransform変数
	//============================

#pragma region カメラのTransform変数

	// カメラのTransform初期値
	Transform cameraTransform{
		{ 1.0f, 1.0f, 1.0f },
		{ std::numbers::pi_v<float> / 3.0f, std::numbers::pi_v<float>, 0.0f },
		{ 0.0f, 23.0f, 10.0f }
	};

#pragma endregion

	//=======================
	// ImGuiの切り替え用の変数
	//=======================

	// 0: plane.obj, 1: axis.obj, 2: fence.obj
	int currentModelIndex = 0;

	//=====================
	// メインループ
	//=====================

#pragma region メインループ

	while (true) {

		// Windowsのメッセージ処理
		if (windowsApi->ProcessMessage()) {
			break;
		}

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

		// 選択中のモデルに応じてポインタを切り替える
		Transform* currentTransform = &planeModelTransform;
		Material* currentMaterialData = planeModelMaterialData;

		// switch文での参照ポインタ切替
		switch (currentModelIndex) {
		case 0:
			currentTransform = &planeModelTransform;
			currentMaterialData = planeModelMaterialData;
			break;
		case 1:
			currentTransform = &axisModelTransform;
			currentMaterialData = axisModelMaterialData;
			break;
		case 2:
			currentTransform = &fenceModelTransform;
			currentMaterialData = fenceModelMaterialData;
			break;
		}

		// 開発用UIの処理。実際に開発用のUIを出す場合は、ここをゲーム固有の処理に置き換える
#ifdef USE_IMGUI
		ImGui::Begin("Settings");

		//===========================================
		// ブレンドモード切り替え用UI
		//===========================================

		ImGui::Separator();
		ImGui::Text("- Blend Mode -");
		const char* blendModeItems[] = { "None", "Normal", "Add", "Subtract", "Multiply", "Screen" };
		ImGui::Combo("Blend Mode", &currentBlendMode, blendModeItems, IM_ARRAYSIZE(blendModeItems));

		// ブレンドモードのリセットボタン
		if (ImGui::Button("Reset Blend Mode")) {
			currentBlendMode = kBlendModeNone;
		}

		//===========================================
		// カメラ切り替え用UI
		//===========================================

		ImGui::Separator();
		ImGui::Text("- Camera -");
		ImGui::Checkbox("Use Debug Camera", &useDebugCamera);

		// デバッグカメラのリセットボタン
		if (useDebugCamera) {
			if (ImGui::Button("Reset Debug Camera")) {
				debugCamera->Reset();
			}
		}

		// 通常カメラのリセットボタン
		if (ImGui::Button("Reset Normal Camera")) {
			cameraTransform = { { 1.0f, 1.0f, 1.0f },{ std::numbers::pi_v<float> / 3.0f, std::numbers::pi_v<float>, 0.0f },{ 0.0f, 23.0f, 10.0f } };
		}

		//===========================================
		// モデルの切り替えラジオボタン
		//===========================================

		ImGui::Separator();
		ImGui::Text("-Select Model-");
		ImGui::RadioButton("Plane (plane.obj)", &currentModelIndex, 0);
		ImGui::RadioButton("Axis (axis.obj)", &currentModelIndex, 1);
		ImGui::RadioButton("Fence (fence.obj)", &currentModelIndex, 2);

		// モデル選択のリセットボタン
		if (ImGui::Button("Reset Model Selection")) {
			currentModelIndex = 0;
		}

		// ラジオボタンの操作で currentModelIndex が変更された場合にポインタを再更新
		switch (currentModelIndex) {
		case 0:
			currentTransform = &planeModelTransform;
			currentMaterialData = planeModelMaterialData;
			break;
		case 1:
			currentTransform = &axisModelTransform;
			currentMaterialData = axisModelMaterialData;
			break;
		case 2:
			currentTransform = &fenceModelTransform;
			currentMaterialData = fenceModelMaterialData;
			break;
		}

		//===========================================
		// モデルのトランスフォーム操作UI
		//===========================================

		ImGui::Separator();
		ImGui::Text("-Model Transform-");

		// 各軸の回転を操作するスライダー
		ImGui::SliderFloat3("Model Rotation", &currentTransform->rotate.x, -std::numbers::pi_v<float>, std::numbers::pi_v<float>);

		ImGui::DragFloat3("Model Scale", &currentTransform->scale.x, 0.1f);

		// リセット時の値をY軸180度にする
		if (ImGui::Button("Reset Rotation")) {
			// 完全な0ではなく、正面を向く Y軸180度(std::numbers::pi_v<float>)にリセットする
			currentTransform->rotate = { 0.0f, std::numbers::pi_v<float>, 0.0f };
		}

		// スケールのリセットボタン
		if (ImGui::Button("Reset Scale")) {
			currentTransform->scale = { 1.0f, 1.0f, 1.0f };
		}

		// パーティクル更新のON/OFFチェックボックス
		ImGui::Checkbox("Particles Update", particleManager->GetIsParticlesUpdatePtr());

		// 位置をリセットするボタン
		if (ImGui::Button("Reset Particles")) {
			particleManager->ResetParticles();
		}

		// BillboardのON/OFFチェックボックス
		ImGui::Checkbox("useBillboard", particleManager->GetUseBillboardPtr());

		//===========================================
		// SpriteのUV座標系を動かせる
		//===========================================

		ImGui::Separator();
		ImGui::Text("UV Transform");
		ImGui::DragFloat2("UV Translate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
		ImGui::DragFloat2("UV Scale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
		ImGui::SliderAngle("UV Rotate", &uvTransformSprite.rotate.z);

		// UV Transformのリセットボタン
		if (ImGui::Button("Reset UV Transform")) {
			uvTransformSprite.scale = { 1.0f, 1.0f, 1.0f };
			uvTransformSprite.rotate = { 0.0f, 0.0f, 0.0f };
			uvTransformSprite.translate = { 0.0f, 0.0f, 0.0f };
		}

		//===========================================
		// 色編集用のImGui
		//===========================================

		ImGui::Separator();
		ImGui::ColorEdit4("Color", &currentMaterialData->color.x);

		// マテリアルカラーのリセットボタン
		if (ImGui::Button("Reset Color")) {
			currentMaterialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		}

		//===========================================
		// ライトの設定
		//===========================================

		ImGui::Separator();
		ImGui::Text("Directional Light");

		// ライトの色変更
		ImGui::ColorEdit4("Light Color", &directionalLightData->color.x);

		// ライトの輝度(0.0 ~ 10.0 程度まで動かせるように設定)
		ImGui::SliderFloat("Intensity", &directionalLightData->intensity, 0.0f, 10.0f);

		// ライトの向き(-10.0 ~ 10.0 の範囲で動かす)
		if (ImGui::SliderFloat3("Light Direction", &directionalLightData->direction.x, -10.0f, 10.0f)) {

			// 値が変わったら毎回正規化する
			directionalLightData->direction = MathUtils::Normalize(directionalLightData->direction);
		}

		// ライト設定全体のリセットボタン
		if (ImGui::Button("Reset Light")) {
			directionalLightData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
			directionalLightData->direction = MathUtils::Normalize({ 0.0f, -1.0f, 0.0f });
			directionalLightData->intensity = 1.0f;
		}

		ImGui::End();
#endif

		// === デバッグカメラの更新(行列計算の前に呼び出す)=== //
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

		// === データの計算・定数バッファの更新 === //

		// ビュー行列と射影行列の宣言
		Matrix4x4 viewMatrix;
		Matrix4x4 projectionMatrix;
		Matrix4x4 cameraMatrix;

		if (useDebugCamera) {
			// デバッグカメラの行列を使用
			viewMatrix = debugCamera->GetViewMatrix();
			projectionMatrix = debugCamera->GetProjectionMatrix();
			// ビュー行列の逆行列からカメラ行列を取得
			cameraMatrix = MathUtils::Inverse(viewMatrix);
		} else {
			// 通常カメラの行列を使用
			cameraMatrix = MathUtils::MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
			viewMatrix = MathUtils::Inverse(cameraMatrix);
			projectionMatrix = MathUtils::MakePerspectiveFovMatrix(0.45f, static_cast<float>(WindowsApi::kClientWidth) / static_cast<float>(WindowsApi::kClientHeight), 0.1f, 100.0f);
		}

		// ViewProjectionMatrixの計算
		Matrix4x4 viewProjectionMatrix = MathUtils::Multiply(viewMatrix, projectionMatrix);

		// パーティクルの更新処理
		particleManager->Update(viewProjectionMatrix, cameraMatrix, currentTransform, kDeltaTime);

		// UVTransform行列の作成と更新
		Matrix4x4 uvTransformMatrix = MathUtils::MakeScaleMatrix(uvTransformSprite.scale);
		uvTransformMatrix = MathUtils::Multiply(uvTransformMatrix, MathUtils::MakeRotateZMatrix(uvTransformSprite.rotate.z));
		uvTransformMatrix = MathUtils::Multiply(uvTransformMatrix, MathUtils::MakeTranslateMatrix(uvTransformSprite.translate));

		if (currentModelIndex == 0) {
			planeModelMaterialData->uvTransform = uvTransformMatrix;
		} else if (currentModelIndex == 1) {
			axisModelMaterialData->uvTransform = uvTransformMatrix;
		} else {
			fenceModelMaterialData->uvTransform = uvTransformMatrix;
		}

		// 描画用の頂点数を決定
		UINT activeVertexCount = 0;
		if (currentModelIndex == 0) {
			activeVertexCount = UINT(planeModelData.vertices.size());
		} else if (currentModelIndex == 1) {
			activeVertexCount = UINT(axisModelData.vertices.size());
		} else {
			activeVertexCount = UINT(fenceModelData.vertices.size());
		}

		// ImGuiの内部コマンドを生成する
#ifdef USE_IMGUI
		ImGui::Render();
#endif

		// 毎フレームのループ内で入力を更新
		input->Update();

		// 数字の0キーが押されていたら(トリガー処理)
		if (input->IsTriggerKey(DIK_0)) {
			// 出力ウィンドウに「hit 0」と表示
			OutputDebugStringA("hit 0\n");
		}

		// 描画前処理
		dxPlatform->PreDraw();

		// RootSignatureを設定する。PSOにも設定しているが、別途設定が必要
		commandList->SetGraphicsRootSignature(rootSignature.Get());

		// PSOを設定する
		commandList->SetPipelineState(graphicsPipelineStates[currentBlendMode].Get());

		// トポロジ(形状)を設定する。PSOに設定しているものとはまた別。同じものを設定すると考えておくといい
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		//==========================================
		// モデル(3D)の描画設定(モデル切り替えも対応)
		//==========================================

		// instancing用のDataを読むためにStructuredBufferのSRVを設定する
		commandList->SetGraphicsRootDescriptorTable(1, particleManager->GetSrvHandleGPU());

		// 選択されているモデルに応じて【マテリアル・頂点バッファ・テクスチャ】をすべて正しく切り替える
		if (currentModelIndex == 0) {
			// Plane の描画設定
			commandList->SetGraphicsRootConstantBufferView(0, planeModelMaterialResource.Get()->GetGPUVirtualAddress());
			commandList->IASetVertexBuffers(0, 1, &planeModelVertexBufferView);
			commandList->SetGraphicsRootDescriptorTable(2, dxPlatform->GetSRVGPUDescriptorHandle(1));
		} else if (currentModelIndex == 1) {
			// Axisの描画設定
			commandList->SetGraphicsRootConstantBufferView(0, axisModelMaterialResource.Get()->GetGPUVirtualAddress());
			commandList->IASetVertexBuffers(0, 1, &axisModelVertexBufferView);
			commandList->SetGraphicsRootDescriptorTable(2, dxPlatform->GetSRVGPUDescriptorHandle(2));
		} else if (currentModelIndex == 2) {
			// Fenceの描画設定
			commandList->SetGraphicsRootConstantBufferView(0, fenceModelMaterialResource.Get()->GetGPUVirtualAddress());
			commandList->IASetVertexBuffers(0, 1, &fenceModelVertexBufferView);
			commandList->SetGraphicsRootDescriptorTable(2, dxPlatform->GetSRVGPUDescriptorHandle(3));
		}

		// plane.obj(currentModelIndex == 0)のみパーティクルの生きている数をインスタンス描画し、他モデルは1枚描画する
		uint32_t instanceCount = (currentModelIndex == 0) ? particleManager->GetNumInstance() : 1;

		// 描画!板ポリゴンをインスタンス描画
		commandList->DrawInstanced(activeVertexCount, instanceCount, 0, 0);

		// 実際のcommandListのImGui描画コマンドを積む
#ifdef USE_IMGUI
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#endif

#pragma endregion

		// 描画後処理
		dxPlatform->PostDraw();

		// 終了時に記録する
		Logger::Log(logStream, "Game Engine Terminated.");
	}

	//===========
	// 解放処理
	//===========

#pragma region 解放処理

	//===========================
	// 入力管理クラスの解放処理
	//===========================

	if (input) {
		// クラス内部のDirectInputデバイスなどの解放関数を呼ぶ
		input->Finalize();
		// newしたメモリ領域を明示的に削除して解放する
		delete input;
		// 安全のためにポインタをクリアしておく
		input = nullptr;
	}

	// 音声データの解放
	audioManager->SoundUnload(&soundData1);

	// XAudio2の解放処理
	audioManager->Finalize();

	// DirectXPlatformの解放
	delete dxPlatform;
	dxPlatform = nullptr;

	//===========================
	// WindowsApiクラスの解放処理
	//===========================

	// WindowsAPIの終了処理
	windowsApi->Finalize();

	delete windowsApi;
	windowsApi = nullptr;

#pragma endregion

	return 0;
}

#pragma endregion