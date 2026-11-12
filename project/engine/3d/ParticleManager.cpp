#include "ParticleManager.h"
#include "DirectXPlatform.h"
#include "Easing.h"
#include <numbers>
#include <cmath>
#include <algorithm>

// AABBとPointの衝突判定関数
bool ParticleManager::IsCollision(const AABB& aabb, const Vector3& point) {
	return (point.x >= aabb.min.x && point.x <= aabb.max.x &&
			point.y >= aabb.min.y && point.y <= aabb.max.y &&
			point.z >= aabb.min.z && point.z <= aabb.max.z);
}

// パーティクル生成関数
Particle ParticleManager::MakeNewParticle(const Vector3& translate) {

	// 位置と速度を{-1.0f, 1.0f}でランダムに初期化する
	std::uniform_real_distribution<float> randomDistribution(-1.0f, 1.0f);

	// 色をランダムに初期化する
	std::uniform_real_distribution<float> distColor(0.0f, 1.0f);

	// 生存可能な時間を{1.0f, 3.0f}でランダムに初期化する
	std::uniform_real_distribution<float> distLifeTime(1.0f, 3.0f);

	Particle particle;
	particle.transform.scale = { 1.0f, 1.0f, 1.0f };
	particle.transform.rotate = { 0.0f, 0.0f, 0.0f };

	// ランダムなオフセット位置を生成してエミッタの基準位置を加算する
	Vector3 randomTranslate{ randomDistribution(mRandomEngine), randomDistribution(mRandomEngine), randomDistribution(mRandomEngine) };
	particle.transform.translate = translate + randomTranslate;

	particle.velocity = { randomDistribution(mRandomEngine), randomDistribution(mRandomEngine), randomDistribution(mRandomEngine) };
	particle.color = { distColor(mRandomEngine), distColor(mRandomEngine), distColor(mRandomEngine), 1.0f };
	particle.lifeTime = distLifeTime(mRandomEngine);
	particle.currentTime = 0.0f;
	particle.isEnergyShower = false;

	return particle;
}

// Emitterでパーティクルを生成し、spliceで結合する関数
void ParticleManager::Emit(const Emitter& emitter) {

	std::list<Particle> newParticles;

	for (uint32_t count = 0; count < emitter.count; ++count) {
		newParticles.push_back(MakeNewParticle(emitter.transform.translate));
	}

	// splice関数で生成したリストを末尾に一括連結
	particles.splice(particles.end(), newParticles);
}

// エナジーシャワー風演出の生成関数
void ParticleManager::EmitEnergyShower(const Vector3& centerPos, uint32_t count) {

	// エナジーシャワー発射時のみ画面上の旧パーティクルを消去する
	particles.clear();

	std::uniform_real_distribution<float> distColor(0.0f, 1.0f);

	// 奥で広がる円の半径(数値を小さくすると三角形同士の間隔が狭まる)
	float radius = 2.8f;

	for (uint32_t i = 0; i < count; ++i) {

		Particle p;
		p.isEnergyShower = true;
		p.startPos = centerPos;

		// 円周上の角度(0 ~ 2π)
		float angle = (2.0f * std::numbers::pi_v<float> / count) * i;

		// 奥の平面(Z軸方向)で円状に展開する目標位置
		p.targetPos = {
			centerPos.x + radius * std::cos(angle),
			centerPos.y + radius * std::sin(angle),
			centerPos.z
		};

		p.transform.scale = { 1.5f, 1.5f, 1.5f };
		p.transform.rotate = { 0.0f, 0.0f, 0.0f };

		// 奥の中心から発生
		p.transform.translate = centerPos;

		// 通常演出と同じランダムカラー設定
		p.color = { distColor(mRandomEngine), distColor(mRandomEngine), distColor(mRandomEngine), 1.0f };

		p.lifeTime = 6.0f;
		p.currentTime = 0.0f;
		p.stateTimer = 0.0f;

		// 時計回りに連続して飛んでいくようディレイを個別に設定
		p.launchDelay = i * 0.07f;
		p.state = EnergyShowerState::Expand;

		particles.push_back(p);
	}
}

// 初期化関数
void ParticleManager::Initialize(DirectXPlatform* dxPlatform, uint32_t descriptorIndex) {

	// 乱数生成器の初期化
	std::random_device seedGenerator;
	mRandomEngine.seed(seedGenerator());

	// AccelerationFieldの初期設定(+x方向に 15m/s^2 の加速度、範囲は原点中心に -1m ~ 1m)
	mAccelerationField.acceleration = { 15.0f, 0.0f, 0.0f };
	mAccelerationField.area.min = { -1.0f, -1.0f, -1.0f };
	mAccelerationField.area.max = { 1.0f, 1.0f, 1.0f };

	// インスタンスのTransformMatrixリソースを作る
	mInstancingResource = dxPlatform->CreateBufferResource(sizeof(ParticleForGPU) * kNumMaxInstance);

	// 書き込むためのアドレスを取得する
	mInstancingResource.Get()->Map(0, nullptr, reinterpret_cast<void**>(&mInstancingData));

	// 単位行列を書き込む
	for (uint32_t index = 0; index < kNumMaxInstance; index++) {
		mInstancingData[index].wvp = MathUtils::MakeIdentity4x4();
		mInstancingData[index].world = MathUtils::MakeIdentity4x4();
		mInstancingData[index].color = { 1.0f, 1.0f, 1.0f, 1.0f };
	}

	//==================================================
	// Instancing用のTransformMatrixResourceのSRVを作成
	//==================================================

	D3D12_SHADER_RESOURCE_VIEW_DESC instancingSrvDesc{};
	instancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	instancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	instancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	instancingSrvDesc.Buffer.FirstElement = 0;
	instancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	instancingSrvDesc.Buffer.NumElements = kNumMaxInstance;
	instancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);

	D3D12_CPU_DESCRIPTOR_HANDLE instancingSrvHandleCPU = dxPlatform->GetSRVCPUDescriptorHandle(descriptorIndex);
	mInstancingSrvHandleGPU = dxPlatform->GetSRVGPUDescriptorHandle(descriptorIndex);

	// SRVを作成
	dxPlatform->GetDevice()->CreateShaderResourceView(mInstancingResource.Get(), &instancingSrvDesc, instancingSrvHandleCPU);

	// Instancing用に最大数分のTransformを用意し、それぞれ位置が少しずつずれるように初期化する
	for (uint32_t index = 0; index < kNumMaxInstance; ++index) {
		particles.push_back(MakeNewParticle());
	}
}

// 位置をリセットする関数
void ParticleManager::ResetParticles() {
	particles.clear();
	for (uint32_t index = 0; index < kNumMaxInstance; ++index) {
		particles.push_back(MakeNewParticle());
	}
}

// 更新関数
void ParticleManager::Update(const Matrix4x4& viewProjectionMatrix, const Matrix4x4& cameraMatrix, const Transform& currentTransform, float kDeltaTime) {

	// 描画すべきインスタンス数
	mNumInstance = 0;

	// ビルボード行列の計算
	Matrix4x4 billboardMatrix = MathUtils::MakeIdentity4x4();

	if (mUseBillboard) {

		// 裏が見えているのでくるっと反対側に回す回転行列を作成する
		Matrix4x4 backToFrontMatrix = MathUtils::MakeRotateYMatrix(std::numbers::pi_v<float>);

		// BillboardMatrixの計算
		billboardMatrix = MathUtils::Multiply(backToFrontMatrix, cameraMatrix);

		// 平行移動成分はいらない
		billboardMatrix.m[3][0] = 0.0f;
		billboardMatrix.m[3][1] = 0.0f;
		billboardMatrix.m[3][2] = 0.0f;
	}

	// ビルボードの右・上方向ベクトル(ワールド空間)を取得して2D射影軸として活用する
	// WVP等を計算して、Resourceに書き込む
	for (std::list<Particle>::iterator particleIterator = particles.begin(); particleIterator != particles.end(); ) {

		// 生存時間を過ぎていたら更新せず、描画対象にしない(生存期間が過ぎたParticleはlistから消す。戻り値が次のイテレータとなる)
		if (particleIterator->lifeTime <= particleIterator->currentTime) {
			particleIterator = particles.erase(particleIterator);
			continue;
		}

		// mIsParticlesUpdateがtrueの時のみ移動と時間経過を行う
		if (mIsParticlesUpdate) {

			if (particleIterator->isEnergyShower) {

				// --- エナジーシャワー風演出の更新 ---
				particleIterator->stateTimer += kDeltaTime;
				particleIterator->currentTime += kDeltaTime;

				switch (particleIterator->state) {
				case EnergyShowerState::Expand: {

					// 奥でスッと円形に広がるフェーズ(0.35秒)
					float duration = 0.35f;
					float t = particleIterator->stateTimer / duration;

					if (t > 1.0f) {
						t = 1.0f;
					}

					// イージング(EaseOutQuart)で円状へ補間移動
					float easeT = Easing::EaseOutQuart(t);
					particleIterator->transform.translate = Easing::Lerp(particleIterator->startPos, particleIterator->targetPos, easeT);

					// 放射状(外側)を向くようにZ軸回転
					Vector3 diff = particleIterator->targetPos - particleIterator->startPos;
					particleIterator->transform.rotate.z = std::atan2f(diff.y, diff.x) - (std::numbers::pi_v<float> / 2.0f);

					if (t >= 1.0f) {
						particleIterator->state = EnergyShowerState::Charge;
						particleIterator->stateTimer = 0.0f;
					}
					break;
				}
				case EnergyShowerState::Charge: {

					// 溜め・発射待ちフェーズ
					if (particleIterator->stateTimer >= (0.22f + particleIterator->launchDelay)) {
						particleIterator->state = EnergyShowerState::Fly;
						particleIterator->stateTimer = 0.0f;

						// 展開位置の左右の広がりを少しだけ中央へ寄せつつ、手前へゆるやかに滑空する方向を設定する
						float offsetX = particleIterator->transform.translate.x - particleIterator->startPos.x;
						float localVectorX = offsetX * 0.12f;

						// 真下ではなく、ゆるやかに下へ滑空する角度
						float localVectorY = -0.28f;

						// カメラ手前方向へ真っ直ぐ飛ぶ成分
						float localVectorZ = 1.0f;

						// ビルボード空間の滑空ベクトルをワールド空間へ変換して正規化する
						Vector3 direction = MathUtils::TransformMatrix({ localVectorX, localVectorY, localVectorZ }, billboardMatrix);
						direction = MathUtils::Normalize(direction);

						// 紙飛行機のように一定の角度で手前へ滑空するスピード
						float speed = 34.0f;
						particleIterator->velocity = direction * speed;

						// 全ての三角形の先端(尖っている方)が進行方向(手前)をピタッと向くようZ軸・X軸の回転角を算出する
						float horizontalLength = std::sqrt(localVectorX * localVectorX + localVectorY * localVectorY);
						particleIterator->transform.rotate.z = std::atan2f(localVectorY, localVectorX) - (std::numbers::pi_v<float> / 2.0f);
						particleIterator->transform.rotate.x = std::atan2f(localVectorZ, horizontalLength);
					}
					break;
				}
				case EnergyShowerState::Fly: {

					// そのままの角度で紙飛行機のように手前へ飛翔するフェーズ
					particleIterator->transform.translate += particleIterator->velocity * kDeltaTime;

					// 画面手前を通り抜けて見えなくなったら消去する
					if (particleIterator->stateTimer > 1.1f) {
						particleIterator->currentTime = particleIterator->lifeTime;
					}
					break;
				}
				}

			} else {
				// --- 通常パーティクルの更新(uvChecker.png用の挙動) ---
				if (mUseAccelerationField) {
					if (IsCollision(mAccelerationField.area, particleIterator->transform.translate)) {
						particleIterator->velocity += mAccelerationField.acceleration * kDeltaTime;
					}
				}

				particleIterator->transform.translate += particleIterator->velocity * kDeltaTime;
				particleIterator->currentTime += kDeltaTime;
			}
		}

		// GPUのリソース上限数(kNumMaxInstance)を超えて書き込まないようにバッファオーバーラン対策
		if (mNumInstance < kNumMaxInstance) {

			// 操作用Transform(currentTransform)と各インスタンスのTransformを合成する
			Vector3 scale = { particleIterator->transform.scale.x * currentTransform.scale.x, particleIterator->transform.scale.y * currentTransform.scale.y, particleIterator->transform.scale.z * currentTransform.scale.z };
			Vector3 rotate = particleIterator->transform.rotate + currentTransform.rotate;
			Vector3 translate = particleIterator->transform.translate + currentTransform.translate;

			// ワールド行列の計算
			Matrix4x4 worldMatrix;

			if (mUseBillboard) {
				Matrix4x4 scaleMatrix = MathUtils::MakeScaleMatrix(scale);
				// 各パーティクルの向き(X軸・Z軸回転)を適用
				Matrix4x4 rotateXMatrix = MathUtils::MakeRotateXMatrix(rotate.x);
				Matrix4x4 rotateZMatrix = MathUtils::MakeRotateZMatrix(rotate.z);
				Matrix4x4 translateMatrix = MathUtils::MakeTranslateMatrix(translate);

				// scale * rotateX * rotateZ * billboard * translate
				worldMatrix = MathUtils::Multiply(scaleMatrix, MathUtils::Multiply(rotateXMatrix, MathUtils::Multiply(rotateZMatrix, MathUtils::Multiply(billboardMatrix, translateMatrix))));
			} else {
				worldMatrix = MathUtils::MakeAffineMatrix(scale, rotate, translate);
			}

			Matrix4x4 worldViewProjectionMatrix = MathUtils::Multiply(worldMatrix, viewProjectionMatrix);

			mInstancingData[mNumInstance].wvp = worldViewProjectionMatrix;
			mInstancingData[mNumInstance].world = worldMatrix;
			mInstancingData[mNumInstance].color = particleIterator->color;

			// アルファ値の計算
			float alpha = 1.0f;
			if (particleIterator->isEnergyShower) {
				if (particleIterator->state == EnergyShowerState::Fly) {
					// 画面手前に接近した際のフェードアウト
					if (particleIterator->stateTimer > 0.75f) {
						float fadeT = std::clamp((particleIterator->stateTimer - 0.75f) / 0.35f, 0.0f, 1.0f);
						alpha = Easing::Lerp(1.0f, 0.0f, fadeT);
					}
				}
				mInstancingData[mNumInstance].color.w = alpha;
			} else {
				float t = particleIterator->currentTime / particleIterator->lifeTime;
				alpha = 1.0f - Easing::EaseInQuart(t);
				mInstancingData[mNumInstance].color.w = alpha;
			}

			// 生きているパーティクルの数を1つカウントする
			++mNumInstance;
		}
		// 次のパーティクルに進める
		++particleIterator;
	}
}