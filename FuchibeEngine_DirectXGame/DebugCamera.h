#pragma once
#include "MathUtils.h"

class Input;

class DebugCamera {
public:
    // 初期化関数
    void Initialize();

    // 更新関数
    void Update(Input* input);

    // カメラのリセット関数
    void Reset();

    // ゲッター
    const Matrix4x4& GetViewMatrix() const { return mViewMatrix; }
    const Matrix4x4& GetProjectionMatrix() const { return mProjectionMatrix; }
    const Vector3& GetTranslation() const { return mTranslation; }
    const Matrix4x4& GetRotationMatrix() const { return mMatRot; }

private:
    // 回転行列(単位行列で初期化)
    Matrix4x4 mMatRot = MathUtils::MakeIdentity4x4();

    // ローカル座標
    Vector3 mTranslation = { 0.0f, 0.0f, -50.0f };

    // ビュー行列(単位行列で初期化)
    Matrix4x4 mViewMatrix = MathUtils::MakeIdentity4x4();

    // 射影行列(単位行列で初期化)
    Matrix4x4 mProjectionMatrix = MathUtils::MakeIdentity4x4();
};