#pragma once
#include <cmath>
#include <string>
#include <vector>
#include <cstdint>

// 4x4行列
struct Matrix4x4 {
	float m[4][4];
};

struct Matrix3x3 {
	float m[3][3];
};

// Vector4構造体
struct Vector4 {
	float x, y, z, w;
};

// Vector2構造体
struct Vector2 {
	float x, y;
};

struct Vector3 {
	float x;
	float y;
	float z;

	// + 演算子のオーバーロード
	Vector3 operator+(const Vector3& obj) const {
		Vector3 sc;
		sc.x = this->x + obj.x;
		sc.y = this->y + obj.y;
		sc.z = this->z + obj.z;
		return sc;
	}

	// += 演算子のオーバーロード
	Vector3& operator+=(const Vector3& obj) {
		x += obj.x;
		y += obj.y;
		z += obj.z;
		return *this;
	}

	// - 演算子のオーバーロード
	Vector3 operator-(const Vector3& obj) const {
		Vector3 sc;
		sc.x = this->x - obj.x;
		sc.y = this->y - obj.y;
		sc.z = this->z - obj.z;
		return sc;
	}

	// Vector3 * float の掛け算オーバーロード
	Vector3 operator*(float s) const {
		Vector3 sc;
		sc.x = this->x * s;
		sc.y = this->y * s;
		sc.z = this->z * s;
		return sc;
	}
};

struct Transform {
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};

// 頂点データ
struct VertexData {
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};

// マテリアル
struct Material {
	Vector4 color;
	int32_t enableLighting;
	float padding[3];
	Matrix4x4 uvTransform;
};

// UVTransform用の変数
inline Transform uvTransformSprite{
	{ 1.0f, 1.0f, 1.0f },
	{ 0.0f, 0.0f, 0.0f },
	{ 0.0f, 0.0f, 0.0f },
};

// TransformationMatrix
struct TransformationMatrix {
	Matrix4x4 wvp;
	Matrix4x4 world;
};

// 平行光源
struct DirectionalLight {
	// ライトの色
	Vector4 color;
	// ライトの向き
	Vector3 direction;
	// 輝度(明るさ)
	float intensity;
};

// マテリアルデータ
struct MaterialData {
	std::string textureFilePath;
};

// モデルデータ
struct ModelData {
	std::vector<VertexData> vertices;
	MaterialData material;
};

struct Camera {
	Vector3 pos;
};

class MathUtils {
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
	static Matrix4x4 Multiply(Matrix4x4 matrix1, Matrix4x4 matrix2);

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