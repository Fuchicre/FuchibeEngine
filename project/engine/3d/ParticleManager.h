#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <random>
#include <cstdint>
#include <list>
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

// エミッタ構造体
struct Emitter {
	// エミッタのTransform
	Transform transform;
	// 発生数
	uint32_t count;
	// 発生頻度
	float frequency;
	// 頻度用時刻
	float frequencyTime;
};

class ParticleManager {
public:

	// 初期化関数
	void Initialize(DirectXPlatform* dxPlatform, uint32_t descriptorIndex);

	// 更新関数
	void Update(const Matrix4x4& viewProjectionMatrix, const Matrix4x4& cameraMatrix, const Transform& currentTransform, float kDeltaTime);

	// パーティクル生成関数
	Particle MakeNewParticle(const Vector3& translate = { 0.0f, 0.0f, 0.0f });

	// Emitterでパーティクルを生成して内部リストに追加する関数
	void Emit(const Emitter& emitter);

	// 位置をリセットする関数
	void ResetParticles();

	// パーティクルを1個追加する関数
	void AddParticle(const Particle& particle) { particles.push_back(particle); }

	// ゲッター・セッター
	uint32_t GetNumInstance() const { return mNumInstance; }
	bool GetIsParticlesUpdate() const { return mIsParticlesUpdate; }
	bool* GetIsParticlesUpdatePtr() { return &mIsParticlesUpdate; }
	bool GetUseBillboard() const { return mUseBillboard; }
	bool* GetUseBillboardPtr() { return &mUseBillboard; }
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU() const { return mInstancingSrvHandleGPU; }

public:
	// インスタンス最大数
	static const uint32_t kNumMaxInstance = 100;

private:
	// 乱数生成器
	std::mt19937 mRandomEngine;

	// インスタンスのリスト
	std::list<Particle> particles;

	// インスタンスのTransformMatrixリソース
	Microsoft::WRL::ComPtr<ID3D12Resource> mInstancingResource;
	ParticleForGPU* mInstancingData = nullptr;

	// SRVのGPUハンドル
	D3D12_GPU_DESCRIPTOR_HANDLE mInstancingSrvHandleGPU{};

	// 描画すべきインスタンス数
	uint32_t mNumInstance = 0;

	// パーティクルの移動更新フラグ(初期値は false = 停止中)
	bool mIsParticlesUpdate = false;

	// ビルボードを使用するかどうかのフラグ(初期値は true)
	bool mUseBillboard = true;
};