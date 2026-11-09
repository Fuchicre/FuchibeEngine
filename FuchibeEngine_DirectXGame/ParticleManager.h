#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <random>
#include <cstdint>
#include "MathUtils.h"

// 前方宣言
class DirectXPlatform;

// パーティクル
struct Particle {
	Transform transform;
	Vector3 velocity;
	Vector4 color;
	// 生存可能な時間
	float lifeTime;
	// 発生してからの経過時間
	float currentTime;
};

// GPUへ転送するデータ用のパーティクル構造体
struct ParticleForGPU {
	Matrix4x4 wvp;
	Matrix4x4 world;
	Vector4 color;
};

class ParticleManager {
public:

	// 初期化関数
	void Initialize(DirectXPlatform* dxPlatform, uint32_t descriptorIndex);

	// 更新関数
	void Update(const Matrix4x4& viewProjectionMatrix, const Transform* currentTransform, float kDeltaTime);

	// パーティクル生成関数
	Particle MakeNewParticle();

	// 位置をリセットする関数
	void ResetParticles();

	// ゲッター・セッター
	uint32_t GetNumInstance() const { return mNumInstance; }
	bool GetIsParticlesUpdate() const { return mIsParticlesUpdate; }
	bool* GetIsParticlesUpdatePtr() { return &mIsParticlesUpdate; }
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU() const { return mInstancingSrvHandleGPU; }

public:
	// インスタンス数
	static const uint32_t kNumMaxInstance = 10;

private:
	// 乱数生成器
	std::mt19937 mRandomEngine;

	// Instancing用に最大数分のTransformを用意する配列
	Particle mParticles[kNumMaxInstance]{};

	// インスタンスのTransformMatrixリソース
	Microsoft::WRL::ComPtr<ID3D12Resource> mInstancingResource;
	ParticleForGPU* mInstancingData = nullptr;

	// SRVのGPUハンドル
	D3D12_GPU_DESCRIPTOR_HANDLE mInstancingSrvHandleGPU{};

	// 描画すべきインスタンス数
	uint32_t mNumInstance = 0;

	// パーティクルの移動更新フラグ(初期値は false = 停止中)
	bool mIsParticlesUpdate = false;
};