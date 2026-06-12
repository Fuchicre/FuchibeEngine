#pragma once
#include <cmath>
#define _USE_MATH_DEFINES

// 4x4行列
struct Matrix4x4 {
	float m[4][4];
};

struct Vector3 {
	float x;
	float y;
	float z;
};

struct Transform {
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};

struct Camera {
	Vector3 pos;
};

class MathUtils{

public:

	// 拡大縮小行列
	static Matrix4x4 MakeScaleMatrix(const Vector3& scale);

	// X軸の回転行列の作成関数
	static Matrix4x4 MakeRotateXMatrix(float theta);

	// Y軸の回転行列の作成関数
	static Matrix4x4 MakeRotateYMatrix(float theta);

	// Z軸の回転行列の作成関数
	static Matrix4x4 MakeRotateZMatrix(float theta);

	// 平行移動行列
	static Matrix4x4 MakeTranslateMatrix(const Vector3& translate);

	// Matrix4x4の乗算行列の作成関数
	/*static Matrix4x4 MultiplyMatrix4x4(Matrix4x4 matrix1, Matrix4x4 matrix2);*/
	static Matrix4x4 Multiply(Matrix4x4 matrix1, Matrix4x4 matrix2);

	// Vector同士の乗算行列の作成関数
	/*static Vector3 MultiplyVector(Vector3 vector1, Vector3 vector2, Vector3 vector3);*/

	// アフィン変換行列の作成関数
	static Matrix4x4 MakeAffineMatrix(Vector3 scale, Vector3 rotateXYZ, Vector3 translate);

	// 透視投影行列を作成する関数
	static Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);

	// 正射影行列を作成する関数
	static Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);

	// ビューポート変換行列を作成する関数
	static Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);

	// 逆行列の作成関数
	static Matrix4x4 Inverse(Matrix4x4 matrix);

	// 単位行列の作成
	static Matrix4x4 MakeIdentity4x4();

	// 座標変換行列
	static Vector3 TransformMatrix(const Vector3& vector, const Matrix4x4& matrix);

	// 長さ(ノルム)
	static float Length(const Vector3& vector);

	// 正規化関数
	static Vector3 Normalize(const Vector3& vector);

public:

	static Camera camera;

};