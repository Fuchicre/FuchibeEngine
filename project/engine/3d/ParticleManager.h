#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <random>
#include <cstdint>
#include <list>
#include "MathUtils.h"

// 前方宣言
class DirectXPlatform;

// エナジーシャワー風演出のフェーズ
enum class EnergyShowerState {
	// 奥での円形展開
	Expand,
	// 溜め・待機
	Charge,
	// 手前への高速飛翔
	Fly
};

// パーティクル
struct Particle {

	Transform transform{};
	Vector3 velocity{ 0.0f, 0.0f, 0.0f };
	Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };

	// 生存可能な時間
	float lifeTime = 0.0f;

	// 発生してからの経過時間
	float currentTime = 0.0f;

	// エナジーシャワー風演出用データ
	bool isEnergyShower = false;
	EnergyShowerState state = EnergyShowerState::Expand;

	// 奥の中心位置
	Vector3 startPos{ 0.0f, 0.0f, 0.0f };

	// 奥での円形配置目標位置
	Vector3 targetPos{ 0.0f, 0.0f, 0.0f };

	// フェーズ内の経過時間
	float stateTimer = 0.0f;

	// 発射までのディレイ時間
	float launchDelay = 0.0f;
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

// 風を表現するField
struct AccelerationField {
	// 加速度
	Vector3 acceleration;
	// 範囲
	AABB area;
};

class ParticleManager {
public:

	// AABBとPointの衝突判定関数
	bool IsCollision(const AABB& aabb, const Vector3& point);

	// パーティクル生成関数
	Particle MakeNewParticle(const Vector3& translate = { 0.0f, 0.0f, 0.0f });

	// Emitterでパーティクルを生成して内部リストに追加する関数
	void Emit(const Emitter& emitter);

	// エナジーシャワー風演出の生成関数
	void EmitEnergyShower(const Vector3& centerPos = { 0.0f, -2.0f, -10.0f }, uint32_t count = 24);

	// 初期化関数
	void Initialize(DirectXPlatform* dxPlatform, uint32_t descriptorIndex);

	// 更新関数
	void Update(const Matrix4x4& viewProjectionMatrix, const Matrix4x4& cameraMatrix, const Transform& currentTransform, float kDeltaTime);

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

	// AccelerationFieldのゲッター・セッター
	bool GetUseAccelerationField() const { return mUseAccelerationField; }
	bool* GetUseAccelerationFieldPtr() { return &mUseAccelerationField; }
	AccelerationField& GetAccelerationField() { return mAccelerationField; }

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

	// AccelerationField(風/加速度)を使用するかどうかのフラグ(初期値は false = オフ)
	bool mUseAccelerationField = false;

	// AccelerationFieldのインスタンス
	AccelerationField mAccelerationField{};
};