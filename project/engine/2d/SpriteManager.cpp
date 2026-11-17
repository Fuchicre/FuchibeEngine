#include "SpriteManager.h"
#include <wrl.h>
#include <MathUtils.h>
#include <Logger.h>
#include <cassert>

using namespace Microsoft::WRL;

// 初期化関数
void SpriteManager::Initialize(DirectXPlatform* directXPlatform) {

	// 引数で受け取ってメンバ変数に記録する
	mDXPlatform = directXPlatform;

	// DirectX12デバイスの取得
	device = mDXPlatform->GetDevice();

	// ルートシグネチャの生成
	CreateRootSignature();

	// グラフィックスパイプラインの生成
	CreateGraphicsPipelineState();
}

// 共通描画設定
void SpriteManager::GeneralDrawingSettings(BlendMode blendMode) {

	// 範囲外アクセス防止
	assert(blendMode >= kBlendModeNone && blendMode < kCountOfBlendMode);

	// コマンドリストの取得
	ID3D12GraphicsCommandList* commandList = mDXPlatform->GetCommandList();

	// ルートシグネチャをセットするコマンド
	commandList->SetGraphicsRootSignature(rootSignature.Get());

	// グラフィックスパイプラインステートをセットするコマンド
	commandList->SetPipelineState(graphicsPipelineStates[blendMode].Get());

	// プリミティブトポロジーをセットするコマンド
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

}

// ルートシグネチャの作成関数
void SpriteManager::CreateRootSignature() {

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
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));

#pragma endregion

}

// グラフィックスパイプラインの生成関数
void SpriteManager::CreateGraphicsPipelineState() {

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

	// ブレンドなし
	D3D12_BLEND_DESC noneBlendDesc{};
	noneBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	// 通常ブレンド(デフォルト)
	D3D12_BLEND_DESC normalBlendDesc{};
	normalBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	normalBlendDesc.RenderTarget[0].BlendEnable = true;
	normalBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	normalBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	normalBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	normalBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	normalBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	normalBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

	// 加算ブレンド
	D3D12_BLEND_DESC addBlendDesc = normalBlendDesc;
	addBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;

	// 減算ブレンド
	D3D12_BLEND_DESC subtractBlendDesc = normalBlendDesc;
	subtractBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
	subtractBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;

	// 乗算ブレンド
	D3D12_BLEND_DESC multiplyBlendDesc = normalBlendDesc;
	multiplyBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
	multiplyBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;

	// スクリーンブレンド
	D3D12_BLEND_DESC screenBlendDesc = normalBlendDesc;
	screenBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
	screenBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;

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

	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = mDXPlatform->CompileShader(L"resources/shaders/Object3d.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = mDXPlatform->CompileShader(L"resources/shaders/Object3d.PS.hlsl", L"ps_6_0");
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

	for (size_t i = 0; i < kCountOfBlendMode; i++) {

		// 各ブレンドモードを設定
		graphicsPilelineStateDesc.BlendState = blendDescs[i];

		// 実際に生成する
		HRESULT hr = device->CreateGraphicsPipelineState(&graphicsPilelineStateDesc, IID_PPV_ARGS(&graphicsPipelineStates[i]));
		assert(SUCCEEDED(hr));
	}

#pragma endregion

}