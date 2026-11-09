#include "DebugCamera.h"
#include "Input.h"

// 初期化関数
void DebugCamera::Initialize() {

	// 単位行列で回転を初期化
	mMatRot = MathUtils::MakeIdentity4x4();
	mTranslation = kDefaultTranslation;

	// 射影行列の生成
	float fovY = kDefaultFovY;
	float aspectRatio = kDefaultAspectRatio;
	float nearClip = kDefaultNearClip;
	float farClip = kDefaultFarClip;

	mProjectionMatrix = MathUtils::MakePerspectiveFovMatrix(fovY, aspectRatio, nearClip, farClip);

	// 初回のビュー行列の更新
	UpdateViewMatrix();
}

// 更新関数
void DebugCamera::Update(Input* input) {

	if (!input) {
		return;
	}

	// 回転処理(マウス・矢印キー)
	ProcessRotation(input);

	// 移動処理(WASD / EQ / ズーム)
	ProcessTranslation(input);

	// ビュー行列の更新
	UpdateViewMatrix();
}

// カメラのリセット関数
void DebugCamera::Reset() {
	Initialize();
}

// 回転処理
void DebugCamera::ProcessRotation(Input* input) {

	//=========================
	// 入力処理(回転)
	//==========================

	// マウスによる操作(ホイールズーム / ホイールクリックドラッグ回転)
	Input::MouseMove mouseMove = input->GetMouseMove();

	float mouseRotX = 0.0f;
	float mouseRotY = 0.0f;

	// マウスホイール押下(ボタン2)中のドラッグによる回転
	if (input->IsPressMouse(2)) {
		mouseRotX = -static_cast<float>(mouseMove.lY) * mMouseRotSpeed;
		mouseRotY = -static_cast<float>(mouseMove.lX) * mMouseRotSpeed;
	}

	// 回転処理(矢印キー)
	float keyRotX = 0.0f;
	float keyRotY = 0.0f;

	// 上キーで上を向く
	if (input->IsPressKey(DIK_UPARROW)) {
		keyRotX += mRotSpeed;
	}

	// 下キーで下を向く
	if (input->IsPressKey(DIK_DOWNARROW)) {
		keyRotX -= mRotSpeed;
	}

	// 左キーで左を見る
	if (input->IsPressKey(DIK_LEFTARROW)) {
		keyRotY += mRotSpeed;
	}

	// 右キーで右を見る
	if (input->IsPressKey(DIK_RIGHTARROW)) {
		keyRotY -= mRotSpeed;
	}

	// 合計の回転量を算出
	float totalRotX = mouseRotX + keyRotX;
	float totalRotY = mouseRotY + keyRotY;

	if (totalRotX != 0.0f || totalRotY != 0.0f) {

		// 追加回転分の回転行列を生成
		Matrix4x4 matRotDelta = MathUtils::MakeIdentity4x4();
		matRotDelta = MathUtils::Multiply(matRotDelta, MathUtils::MakeRotateXMatrix(totalRotX));
		matRotDelta = MathUtils::Multiply(matRotDelta, MathUtils::MakeRotateYMatrix(totalRotY));

		// 累積の回転行列を合成
		mMatRot = MathUtils::Multiply(matRotDelta, mMatRot);
	}
}

// 移動処理
void DebugCamera::ProcessTranslation(Input* input) {

	//========================
	// 移動処理(WASD / EQ)
	//========================

	Input::MouseMove mouseMove = input->GetMouseMove();

	// マウスホイールによる拡大・縮小
	if (mouseMove.lZ != 0) {
		mTranslation.z += static_cast<float>(mouseMove.lZ) * mZoomSpeed;
	}

	Vector3 move = { 0.0f, 0.0f, 0.0f };

	// 前進
	if (input->IsPressKey(DIK_W)) { move.z += mMoveSpeed; }

	// 後退
	if (input->IsPressKey(DIK_S)) { move.z -= mMoveSpeed; }

	// 右移動
	if (input->IsPressKey(DIK_D)) { move.x += mMoveSpeed; }

	// 左移動
	if (input->IsPressKey(DIK_A)) { move.x -= mMoveSpeed; }

	// 上移動
	if (input->IsPressKey(DIK_E)) { move.y += mMoveSpeed; }

	// 下移動
	if (input->IsPressKey(DIK_Q)) { move.y -= mMoveSpeed; }

	// 移動ベクトルがある場合、カメラの現在の回転行列を使って変換する
	if (move.x != 0.0f || move.y != 0.0f || move.z != 0.0f) {

		Vector3 transformedMove = MathUtils::TransformMatrix(move, mMatRot);

		mTranslation.x += transformedMove.x;
		mTranslation.y += transformedMove.y;
		mTranslation.z += transformedMove.z;
	}
}

// ビュー行列の更新
void DebugCamera::UpdateViewMatrix() {

	//=====================
	// ビュー行列の更新
	//=====================

	Matrix4x4 matTrans = MathUtils::MakeTranslateMatrix(mTranslation);
	Matrix4x4 worldMatrix = MathUtils::Multiply(mMatRot, matTrans);
	mViewMatrix = MathUtils::Inverse(worldMatrix);
}