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

    // 操作パラメータのセッター / ゲッター
    void SetMoveSpeed(float speed) { mMoveSpeed = speed; }
    void SetRotSpeed(float speed) { mRotSpeed = speed; }
    void SetMouseRotSpeed(float speed) { mMouseRotSpeed = speed; }
    void SetZoomSpeed(float speed) { mZoomSpeed = speed; }

    float GetMoveSpeed() const { return mMoveSpeed; }
    float GetRotSpeed() const { return mRotSpeed; }
    float GetMouseRotSpeed() const { return mMouseRotSpeed; }
    float GetZoomSpeed() const { return mZoomSpeed; }

private:
    // 内部処理関数(入力処理と行列計算の分割)
    void ProcessRotation(Input* input);
    void ProcessTranslation(Input* input);
    void UpdateViewMatrix();

private:
    // 定数定義
    static constexpr float kDefaultFovY = 0.45f;
    static constexpr float kDefaultAspectRatio = 1280.0f / 720.0f;
    static constexpr float kDefaultNearClip = 0.1f;
    static constexpr float kDefaultFarClip = 1000.0f;

    static inline const Vector3 kDefaultTranslation = { 0.0f, 0.0f, -50.0f };

    // 操作スピード・感度パラメータ(デフォルト値) //

    // 移動速度
    float mMoveSpeed = 0.5f;

    // 回転速度(ラジアン)
    float mRotSpeed = 0.02f;

    // マウス回転速度
    float mMouseRotSpeed = 0.005f;

    // ズーム速度
    float mZoomSpeed = 0.05f;

    // 回転行列(単位行列で初期化)
    Matrix4x4 mMatRot = MathUtils::MakeIdentity4x4();

    // ローカル座標
    Vector3 mTranslation = kDefaultTranslation;

    // ビュー行列(単位行列で初期化)
    Matrix4x4 mViewMatrix = MathUtils::MakeIdentity4x4();

    // 射影行列(単位行列で初期化)
    Matrix4x4 mProjectionMatrix = MathUtils::MakeIdentity4x4();
};