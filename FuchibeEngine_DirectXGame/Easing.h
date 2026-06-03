#pragma once
#define _USE_MATH_DEFINES
#include <algorithm>
#include <cmath>
#include "MathUtils.h"

// イージングの種類
enum Type { EASE_IN, EASE_OUT, EASE_IN_OUT, EASE_IN_BACK, EASE_OUT_BACK, EASE_IN_QUART, EASE_OUT_QUART, EASE_IN_OUT_QUART };

// イージングの状態
struct State {

	Vector3 start;
	Vector3 end;

	// イージングの現在のフレーム数
	float currentFrame;

	// イージングの終了フレーム数
	float endFrame;

	bool isActive;
	Type type;

	// 初期化関数
	void Initialize();

	// 更新関数
	void FrameCountUp();

	// 現在の補間された座標を計算して返す関数
	Vector3 GetCurrentPosition();
};

class Easing {

public:
	static float Lerp(float start, float end, float frame);
	static Vector3 Lerp(const Vector3& start, const Vector3& end, float frame);
	static float EaseIn(float t);
	static float EaseOut(float t);
	static float EaseInOut(float t);
	static float EaseInBack(float t);
	static float EaseOutBack(float t);
	static float EaseInQuart(float t);
	static float EaseOutQuart(float t);
	static float EaseInOutQuart(float t);
};