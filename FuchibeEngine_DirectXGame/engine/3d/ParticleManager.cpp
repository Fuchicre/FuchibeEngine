#include "ParticleManager.h"
#include "DirectXPlatform.h"
#include "Easing.h"
#include <numbers>

// パーティクル生成関数
Particle ParticleManager::MakeNewParticle() {

	// 位置と速度を{-1.0f, 1.0f}でランダムに初期化する
	std::uniform_real_distribution<float> randomDistribution(-1.0f, 1.0f);

	// 色をランダムに初期化する
	std::uniform_real_distribution<float> distColor(0.0f, 1.0f);

	// 生存可能な時間を{1.0f, 3.0f}でランダムに初期化する
	std::uniform_real_distribution<float> distLifeTime(1.0f, 3.0f);

	Particle particle;
	particle.transform.scale = { 1.0f, 1.0f, 1.0f };
	particle.transform.rotate = { 0.0f, 0.0f, 0.0f };
	particle.transform.translate = { randomDistribution(mRandomEngine), randomDistribution(mRandomEngine), randomDistribution(mRandomEngine) };
	particle.velocity = { randomDistribution(mRandomEngine), randomDistribution(mRandomEngine), randomDistribution(mRandomEngine) };
	particle.color = { distColor(mRandomEngine), distColor(mRandomEngine), distColor(mRandomEngine), 1.0f };
	particle.lifeTime = distLifeTime(mRandomEngine);
	particle.currentTime = 0.0f;

	return particle;
}

// 初期化関数
void ParticleManager::Initialize(DirectXPlatform* dxPlatform, uint32_t descriptorIndex) {

	// 乱数生成器の初期化
	std::random_device seedGenerator;
	mRandomEngine.seed(seedGenerator());

	// インスタンスのTransformMatrixリソースを作る
	mInstancingResource = dxPlatform->CreateBufferResource(sizeof(ParticleForGPU) * kNumMaxInstance);

	// 書き込むためのアドレスを取得
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
		mParticles[index] = MakeNewParticle();
	}
}

// 位置をリセットする関数
void ParticleManager::ResetParticles() {
	for (uint32_t index = 0; index < kNumMaxInstance; ++index) {
		mParticles[index] = MakeNewParticle();
	}
}

// 更新関数
void ParticleManager::Update(const Matrix4x4& viewProjectionMatrix, const Matrix4x4& cameraMatrix, const Transform* currentTransform, float kDeltaTime) {

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

	// WVP等を計算して、Resourceに書き込む
	for (uint32_t index = 0; index < kNumMaxInstance; ++index) {

		// 生存時間を過ぎていたら更新せず、描画対象にしない
		if (mParticles[index].lifeTime <= mParticles[index].currentTime) {
			continue;
		}

		// isParticlesUpdateがtrueの時のみ移動と時間経過を行う
		if (mIsParticlesUpdate) {
			mParticles[index].transform.translate += mParticles[index].velocity * kDeltaTime;

			// 経過時間を加算する
			mParticles[index].currentTime += kDeltaTime;
		}

		// 操作用Transform(currentTransform)と各インスタンスのTransformを合成する
		Vector3 scale = { mParticles[index].transform.scale.x * currentTransform->scale.x, mParticles[index].transform.scale.y * currentTransform->scale.y, mParticles[index].transform.scale.z * currentTransform->scale.z };
		Vector3 rotate = { mParticles[index].transform.rotate.x + currentTransform->rotate.x, mParticles[index].transform.rotate.y + currentTransform->rotate.y, mParticles[index].transform.rotate.z + currentTransform->rotate.z };
		Vector3 translate = { mParticles[index].transform.translate.x + currentTransform->translate.x, mParticles[index].transform.translate.y + currentTransform->translate.y, mParticles[index].transform.translate.z + currentTransform->translate.z };

		// ワールド行列の計算(useBillboardがtrueの場合は scaleMatrix * billboardMatrix * translateMatrix で計算する)
		Matrix4x4 worldMatrix;

		if (mUseBillboard) {
			Matrix4x4 scaleMatrix = MathUtils::MakeScaleMatrix(scale);
			Matrix4x4 translateMatrix = MathUtils::MakeTranslateMatrix(translate);
			worldMatrix = MathUtils::Multiply(scaleMatrix, MathUtils::Multiply(billboardMatrix, translateMatrix));
		} else {
			worldMatrix = MathUtils::MakeAffineMatrix(scale, rotate, translate);
		}

		Matrix4x4 worldViewProjectionMatrix = MathUtils::Multiply(worldMatrix, viewProjectionMatrix);

		mInstancingData[mNumInstance].wvp = worldViewProjectionMatrix;
		mInstancingData[mNumInstance].world = worldMatrix;
		mInstancingData[mNumInstance].color = mParticles[index].color;

		// 経過時間の進行度割合(0.0f ~ 1.0f)を計算
		float t = mParticles[index].currentTime / mParticles[index].lifeTime;

		// 徐々に透明化させるためにイージングを適用する
		// EaseInQuartの場合、消え際に向かって急速に透明化していく
		float alpha = 1.0f - Easing::EaseInQuart(t);

		// 算出したアルファ値をinstancingDataに書き込む
		mInstancingData[mNumInstance].color.w = alpha;

		// 生きているパーティクルの数を1つカウントする
		++mNumInstance;
	}
}