#include "MathUtils.h"
#include <cassert>

Camera MathUtils::mCamera = { { 0.0f, 0.0f, -10.0f } };

// 拡大縮小行列
Matrix4x4 MathUtils::MakeScaleMatrix(const Vector3& scale) {

	Matrix4x4 result{};

	result.m[0][0] = scale.x;
	result.m[0][1] = 0.0f;
	result.m[0][2] = 0.0f;
	result.m[0][3] = 0.0f;

	result.m[1][0] = 0.0f;
	result.m[1][1] = scale.y;
	result.m[1][2] = 0.0f;
	result.m[1][3] = 0.0f;

	result.m[2][0] = 0.0f;
	result.m[2][1] = 0.0f;
	result.m[2][2] = scale.z;
	result.m[2][3] = 0.0f;

	result.m[3][0] = 0.0f;
	result.m[3][1] = 0.0f;
	result.m[3][2] = 0.0f;
	result.m[3][3] = 1.0f;

	return result;
}

// X軸の回転行列の作成関数
Matrix4x4 MathUtils::MakeRotateXMatrix(float theta) {

	Matrix4x4 result = {};

	result.m[0][0] = 1.0f;
	result.m[0][1] = 0.0f;
	result.m[0][2] = 0.0f;
	result.m[0][3] = 0.0f;

	result.m[1][0] = 0.0f;
	result.m[1][1] = cosf(theta);
	result.m[1][2] = sinf(theta);
	result.m[1][3] = 0.0f;

	result.m[2][0] = 0.0f;
	result.m[2][1] = -sinf(theta);
	result.m[2][2] = cosf(theta);
	result.m[2][3] = 0.0f;

	result.m[3][0] = 0.0f;
	result.m[3][1] = 0.0f;
	result.m[3][2] = 0.0f;
	result.m[3][3] = 1.0f;

	return result;

}

// Y軸の回転行列の作成関数
Matrix4x4 MathUtils::MakeRotateYMatrix(float theta) {

	Matrix4x4 result = {};

	result.m[0][0] = cosf(theta);
	result.m[0][1] = 0.0f;
	result.m[0][2] = -sinf(theta);
	result.m[0][3] = 0.0f;

	result.m[1][0] = 0.0f;
	result.m[1][1] = 1.0f;
	result.m[1][2] = 0.0f;
	result.m[1][3] = 0.0f;

	result.m[2][0] = sinf(theta);
	result.m[2][1] = 0.0f;
	result.m[2][2] = cosf(theta);
	result.m[2][3] = 0.0f;

	result.m[3][0] = 0.0f;
	result.m[3][1] = 0.0f;
	result.m[3][2] = 0.0f;
	result.m[3][3] = 1.0f;

	return result;

}

// Z軸の回転行列の作成関数
Matrix4x4 MathUtils::MakeRotateZMatrix(float theta) {

	Matrix4x4 result = {};

	result.m[0][0] = cosf(theta);
	result.m[0][1] = sinf(theta);
	result.m[0][2] = 0.0f;
	result.m[0][3] = 0.0f;

	result.m[1][0] = -sinf(theta);
	result.m[1][1] = cosf(theta);
	result.m[1][2] = 0.0f;
	result.m[1][3] = 0.0f;

	result.m[2][0] = 0.0f;
	result.m[2][1] = 0.0f;
	result.m[2][2] = 1.0f;
	result.m[2][3] = 0.0f;

	result.m[3][0] = 0.0f;
	result.m[3][1] = 0.0f;
	result.m[3][2] = 0.0f;
	result.m[3][3] = 1.0f;

	return result;

}

// 平行移動行列
Matrix4x4 MathUtils::MakeTranslateMatrix(const Vector3& translate) {

	Matrix4x4 result{};

	result.m[0][0] = 1.0f;
	result.m[0][1] = 0.0f;
	result.m[0][2] = 0.0f;
	result.m[0][3] = 0.0f;

	result.m[1][0] = 0.0f;
	result.m[1][1] = 1.0f;
	result.m[1][2] = 0.0f;
	result.m[1][3] = 0.0f;

	result.m[2][0] = 0.0f;
	result.m[2][1] = 0.0f;
	result.m[2][2] = 1.0f;
	result.m[2][3] = 0.0f;

	result.m[3][0] = translate.x;
	result.m[3][1] = translate.y;
	result.m[3][2] = translate.z;
	result.m[3][3] = 1.0f;

	return result;
}

// Matrix4x4の乗算行列の作成関数
Matrix4x4 MathUtils::Multiply(Matrix4x4 matrix1, Matrix4x4 matrix2) {

	Matrix4x4 result = {};

	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			for (int k = 0; k < 4; ++k) {
				result.m[i][j] += matrix1.m[i][k] * matrix2.m[k][j];
			}
		}
	}

	return result;

}

// アフィン変換行列の作成関数
Matrix4x4 MathUtils::MakeAffineMatrix(Vector3 scale, Vector3 rotateXYZ, Vector3 translate) {

	// 各成分の行列を作成する

	// 拡大縮小行列
	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);

	// 回転行列(XYZ軸)
	Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotateXYZ.x);
	Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotateXYZ.y);
	Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotateXYZ.z);

	// 平行移動行列
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);

	// 回転を合成 (X -> Y -> Z の順で掛ける)
	Matrix4x4 rotateMatrix = Multiply(Multiply(rotateXMatrix, rotateYMatrix), rotateZMatrix);

	// 行ベクトル形式なので、左から [Scale] * [Rotate] * [Translate] の順に掛ける
	Matrix4x4 matSR = Multiply(scaleMatrix, rotateMatrix);
	Matrix4x4 result = Multiply(matSR, translateMatrix);

	return result;

}

// 透視投影行列を作成する関数
Matrix4x4 MathUtils::MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip) {

	Matrix4x4 result{};

	float h = 1.0f / tanf(fovY * 0.5f);
	float w = h / aspectRatio;

	result.m[0][0] = w;
	result.m[1][1] = h;
	result.m[2][2] = farClip / (farClip - nearClip);
	result.m[2][3] = 1.0f;

	// 移動成分はここ
	result.m[3][2] = (-nearClip * farClip) / (farClip - nearClip);
	result.m[3][3] = 0.0f;

	return result;

}

// 正射影行列を作成する関数
Matrix4x4 MathUtils::MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {

	Matrix4x4 result{};

	result.m[0][0] = 2.0f / (right - left);
	result.m[1][1] = 2.0f / (top - bottom);
	result.m[2][2] = 1.0f / (farClip - nearClip);
	result.m[3][0] = (left + right) / (left - right);
	result.m[3][1] = (top + bottom) / (bottom - top);
	result.m[3][2] = nearClip / (nearClip - farClip);
	result.m[3][3] = 1.0f;

	return result;
}

// ビューポート変換行列を作成する関数
Matrix4x4 MathUtils::MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth) {

	Matrix4x4 result{};

	result.m[0][0] = width / 2.0f;

	// Y軸下方向反転
	result.m[1][1] = -height / 2.0f;
	result.m[2][2] = (maxDepth - minDepth) / 2.0f;
	result.m[3][0] = left + width / 2.0f;

	// 中心（360）へオフセット
	result.m[3][1] = top + height / 2.0f;
	result.m[3][2] = (maxDepth + minDepth) / 2.0f;
	result.m[3][3] = 1.0f;

	return result;

}

// 逆行列の作成関数
Matrix4x4 MathUtils::Inverse(Matrix4x4 matrix) {

	Matrix4x4 result{};

	// 0行目を除いた小行列式(2行目と3行目の組み合わせ)
	float v0 = matrix.m[2][2] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][2];
	float v1 = matrix.m[2][1] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][1];
	float v2 = matrix.m[2][1] * matrix.m[3][2] - matrix.m[2][2] * matrix.m[3][1];
	float v3 = matrix.m[2][0] * matrix.m[3][3] - matrix.m[2][3] * matrix.m[3][0];
	float v4 = matrix.m[2][0] * matrix.m[3][2] - matrix.m[2][2] * matrix.m[3][0];
	float v5 = matrix.m[2][0] * matrix.m[3][1] - matrix.m[2][1] * matrix.m[3][0];

	// 1行目の要素を掛けて、0列目の余因子を計算
	float t0 = +(matrix.m[1][1] * v0 - matrix.m[1][2] * v1 + matrix.m[1][3] * v2);
	float t1 = -(matrix.m[1][0] * v0 - matrix.m[1][2] * v3 + matrix.m[1][3] * v4);
	float t2 = +(matrix.m[1][0] * v1 - matrix.m[1][1] * v3 + matrix.m[1][3] * v5);
	float t3 = -(matrix.m[1][0] * v2 - matrix.m[1][1] * v4 + matrix.m[1][2] * v5);

	// 行列式(Determinant)の計算
	float det = matrix.m[0][0] * t0 + matrix.m[0][1] * t1 + matrix.m[0][2] * t2 + matrix.m[0][3] * t3;

	// 逆行列が存在するかチェック(0ゼロ除算防止)
	assert((det < -1.0e-6f || 1.0e-6f < det) && "Matrix is singular");

	// 転置を考慮して代入
	result.m[0][0] = t0;
	result.m[1][0] = t1;
	result.m[2][0] = t2;
	result.m[3][0] = t3;

	// 1行目を除いた小行列式(2行目と3行目の組み合わせを0行目で展開)
	result.m[0][1] = -(matrix.m[0][1] * v0 - matrix.m[0][2] * v1 + matrix.m[0][3] * v2);
	result.m[1][1] = +(matrix.m[0][0] * v0 - matrix.m[0][2] * v3 + matrix.m[0][3] * v4);
	result.m[2][1] = -(matrix.m[0][0] * v1 - matrix.m[0][1] * v3 + matrix.m[0][3] * v5);
	result.m[3][1] = +(matrix.m[0][0] * v2 - matrix.m[0][1] * v4 + matrix.m[0][2] * v5);

	// 2行目を除いた小行列式(0行目と1行目の組み合わせ)
	v0 = matrix.m[0][2] * matrix.m[1][3] - matrix.m[0][3] * matrix.m[1][2];
	v1 = matrix.m[0][1] * matrix.m[1][3] - matrix.m[0][3] * matrix.m[1][1];
	v2 = matrix.m[0][1] * matrix.m[1][2] - matrix.m[0][2] * matrix.m[1][1];
	v3 = matrix.m[0][0] * matrix.m[1][3] - matrix.m[0][3] * matrix.m[1][0];
	v4 = matrix.m[0][0] * matrix.m[1][2] - matrix.m[0][2] * matrix.m[1][0];
	v5 = matrix.m[0][0] * matrix.m[1][1] - matrix.m[0][1] * matrix.m[1][0];

	// 3行目の要素を掛けて計算
	result.m[0][2] = +(matrix.m[3][1] * v0 - matrix.m[3][2] * v1 + matrix.m[3][3] * v2);
	result.m[1][2] = -(matrix.m[3][0] * v0 - matrix.m[3][2] * v3 + matrix.m[3][3] * v4);
	result.m[2][2] = +(matrix.m[3][0] * v1 - matrix.m[3][1] * v3 + matrix.m[3][3] * v5);
	result.m[3][2] = -(matrix.m[3][0] * v2 - matrix.m[3][1] * v4 + matrix.m[3][2] * v5);

	// 3行目を除いた小行列式(0行目と1行目の組み合わせを2行目で展開)
	result.m[0][3] = -(matrix.m[2][1] * v0 - matrix.m[2][2] * v1 + matrix.m[2][3] * v2);
	result.m[1][3] = +(matrix.m[2][0] * v0 - matrix.m[2][2] * v3 + matrix.m[2][3] * v4);
	result.m[2][3] = -(matrix.m[2][0] * v1 - matrix.m[2][1] * v3 + matrix.m[2][3] * v5);
	result.m[3][3] = +(matrix.m[2][0] * v2 - matrix.m[2][1] * v4 + matrix.m[2][2] * v5);

	// 行列式で割る
	float invDet = 1.0f / det;

	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			result.m[y][x] *= invDet;
		}
	}

	return result;
}

// 単位行列の作成
Matrix4x4 MathUtils::MakeIdentity4x4() {

	Matrix4x4 result{};

	// 対角成分 ([0][0]), ([1][1]), ([2][2]), ([3][3]) を 1.0f にする
	result.m[0][0] = 1.0f;
	result.m[1][1] = 1.0f;
	result.m[2][2] = 1.0f;
	result.m[3][3] = 1.0f;

	return result;

}

// 座標変換行列
Vector3 MathUtils::TransformMatrix(const Vector3& vector, const Matrix4x4& matrix) {

	Vector3 result{};

	result.x = vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0] + matrix.m[3][0];
	result.y = vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1] + matrix.m[3][1];
	result.z = vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2] + matrix.m[3][2];

	float w = vector.x * matrix.m[0][3] + vector.y * matrix.m[1][3] + vector.z * matrix.m[2][3] + matrix.m[3][3];

	if (w != 0.0f) {
		result.x /= w;
		result.y /= w;
		result.z /= w;
	}

	return result;

}

// 長さ(ノルム)
float MathUtils::Length(const Vector3& vector) {
	return sqrtf(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
}

// 正規化関数
Vector3 MathUtils::Normalize(const Vector3& vector) {

	float length = Length(vector);

	// 長さがゼロの場合はゼロベクトルを返す
	if (length == 0.0f) {
		return { 0.0f, 0.0f, 0.0f };
	}

	// 各成分を長さで割る
	Vector3 result;
	result.x = vector.x / length;
	result.y = vector.y / length;
	result.z = vector.z / length;

	return result;

}