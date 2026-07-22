#include "DebugCamera.h"
#include "Input.h"

// 初期化関数
void DebugCamera::Initialize() {

	// 単位行列で回転を初期化
	mMatRot = MathUtils::MakeIdentity4x4();
	mTranslation = { 0.0f, 0.0f, -50.0f };

	// 射影行列の生成
	float fovY = 0.45f;
	float aspectRatio = 1280.0f / 720.0f;
	float nearClip = 0.1f;
	float farClip = 1000.0f;

	mProjectionMatrix = MathUtils::MakePerspectiveFovMatrix(fovY, aspectRatio, nearClip, farClip);

	// 初回のビュー行列の更新
	Matrix4x4 matTrans = MathUtils::MakeTranslateMatrix(mTranslation);
	Matrix4x4 worldMatrix = MathUtils::Multiply(mMatRot, matTrans);
	mViewMatrix = MathUtils::Inverse(worldMatrix);
}

// 更新関数
void DebugCamera::Update(Input* input) {

	if (!input) {
		return;
	}

	//=========================
	// 入力処理(回転・移動)
	//==========================

	// 移動速度
	const float moveSpeed = 0.5f;

	// 回転速度(ラジアン)
	const float rotSpeed = 0.02f;

	// マウスによる操作(ホイールズーム / ホイールクリックドラッグ回転)
	Input::MouseMove mouseMove = input->GetMouseMove();

	// マウスホイールによる拡大・縮小
	if (mouseMove.lZ != 0) {
		const float zoomSpeed = 0.05f;
		mTranslation.z += static_cast<float>(mouseMove.lZ) * zoomSpeed;
	}

	// マウスホイール押下(ボタン2)中のドラッグによる回転
	if (input->IsPressMouse(2)) {
		const float mouseRotSpeed = 0.005f;

		float mouseRotX = -static_cast<float>(mouseMove.lY) * mouseRotSpeed;
		float mouseRotY = -static_cast<float>(mouseMove.lX) * mouseRotSpeed;

		Matrix4x4 matRotDeltaMouse = MathUtils::MakeIdentity4x4();
		matRotDeltaMouse = MathUtils::Multiply(matRotDeltaMouse, MathUtils::MakeRotateXMatrix(mouseRotX));
		matRotDeltaMouse = MathUtils::Multiply(matRotDeltaMouse, MathUtils::MakeRotateYMatrix(mouseRotY));

		mMatRot = MathUtils::Multiply(matRotDeltaMouse, mMatRot);
	}

	// 回転処理(矢印キー)
	float rotX = 0.0f;
	float rotY = 0.0f;

	// 上キーで上を向く
	if (input->IsPress(DIK_UPARROW)) {
		rotX += rotSpeed;

	}

	// 下キーで下を向く
	if (input->IsPress(DIK_DOWNARROW)) {
		rotX -= rotSpeed;
	}

	// 左キーで左を見る
	if (input->IsPress(DIK_LEFTARROW)) {
		rotY += rotSpeed;
	}

	// 右キーで右を見る
	if (input->IsPress(DIK_RIGHTARROW)) {
		rotY -= rotSpeed;
	}

	// 追加回転分の回転行列を生成
	Matrix4x4 matRotDelta = MathUtils::MakeIdentity4x4();
	matRotDelta = MathUtils::Multiply(matRotDelta, MathUtils::MakeRotateXMatrix(rotX));
	matRotDelta = MathUtils::Multiply(matRotDelta, MathUtils::MakeRotateYMatrix(rotY));

	// 累積の回転行列を合成
	mMatRot = MathUtils::Multiply(matRotDelta, mMatRot);

	//========================
	// 移動処理(WASD / EQ)
	//========================

	Vector3 move = { 0.0f, 0.0f, 0.0f };

	// 前進
	if (input->IsPress(DIK_W)) { move.z += moveSpeed; }

	// 後退
	if (input->IsPress(DIK_S)) { move.z -= moveSpeed; }

	// 右移動
	if (input->IsPress(DIK_D)) { move.x += moveSpeed; }

	// 左移動
	if (input->IsPress(DIK_A)) { move.x -= moveSpeed; }

	// 上移動
	if (input->IsPress(DIK_E)) { move.y += moveSpeed; }

	// 下移動
	if (input->IsPress(DIK_Q)) { move.y -= moveSpeed; }

	// 移動ベクトルがある場合、カメラの現在の回転行列を使って変換する
	if (move.x != 0.0f || move.y != 0.0f || move.z != 0.0f) {

		Vector3 transformedMove = MathUtils::TransformMatrix(move, mMatRot);

		mTranslation.x += transformedMove.x;
		mTranslation.y += transformedMove.y;
		mTranslation.z += transformedMove.z;
	}

	//=====================
	// ビュー行列の更新
	//=====================

	Matrix4x4 matTrans = MathUtils::MakeTranslateMatrix(mTranslation);
	Matrix4x4 worldMatrix = MathUtils::Multiply(mMatRot, matTrans);
	mViewMatrix = MathUtils::Inverse(worldMatrix);
}

// カメラのリセット関数
void DebugCamera::Reset() {
	Initialize();
}