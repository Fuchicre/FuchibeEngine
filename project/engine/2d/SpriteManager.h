#pragma once
#include "DirectXPlatform.h"
#include <d3d12.h>
#include <wrl.h>

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
	// ブレンドモードの総数
	kCountOfBlendMode
};

class SpriteManager {
public:
	// 初期化関数
	void Initialize(DirectXPlatform* directXPlatform);

	// DirectXPlatformのゲッター
	DirectXPlatform* GetDirectXPlatform() const { return mDXPlatform; }

	// 共通描画設定
	void GeneralDrawingSettings(BlendMode blendMode = kBlendModeNormal);

private:
	// ルートシグネチャの作成関数
	void CreateRootSignature();

	// グラフィックスパイプラインの生成関数
	void CreateGraphicsPipelineState();

private:
	DirectXPlatform* mDXPlatform = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> materialResourceSprite;

	// DirectX12デバイス
	Microsoft::WRL::ComPtr<ID3D12Device> device;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStates[kCountOfBlendMode] = {};
};