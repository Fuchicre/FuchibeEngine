#include "Easing.h"

// State構造体のメンバ関数
void State::Initialize() {
	start.x = 0.0f;
	start.y = 0.0f;
	start.z = 0.0f;
	end.x = 0.0f;
	end.y = 0.0f;
	end.z = 0.0f;
	currentFrame = 0.0f;
	endFrame = 60.0f;
	isActive = false;
	type = Type::EASE_IN;
}

// 更新関数
void State::FrameCountUp() {

	// アクティブでないなら何もしない
	if (!isActive) {
		return;
	}

	currentFrame++;

	if (currentFrame >= endFrame) {
		currentFrame = endFrame;
		isActive = false;
	}
}

// 現在の補間された座標を計算して返す関数
Vector3 State::GetCurrentPosition() {

	// 割る数が0にならないようにする
	if (endFrame <= 0.0f) {
		return start;
	}

	// 割合を計算する
	float t = currentFrame / endFrame;
	float easedT = 0.0f;

	// タイプ別分岐
	switch (type) {
	case Type::EASE_IN:
		easedT = Easing::EaseIn(t);
		break;
	case Type::EASE_OUT:
		easedT = Easing::EaseOut(t);
		break;
	case Type::EASE_IN_OUT:
		easedT = Easing::EaseInOut(t);
		break;
	case Type::EASE_IN_BACK:
		easedT = Easing::EaseInBack(t);
		break;
	case Type::EASE_OUT_BACK:
		easedT = Easing::EaseOutBack(t);
		break;
	case Type::EASE_IN_QUART:
		easedT = Easing::EaseInQuart(t);
		break;
	case Type::EASE_OUT_QUART:
		easedT = Easing::EaseOutQuart(t);
		break;
	case Type::EASE_IN_OUT_QUART:
		easedT = Easing::EaseInOutQuart(t);
		break;
	}

	// Lerpした座標を返す
	return Easing::Lerp(start, end, easedT);

}

// それぞれのイージング関数
float Easing::Lerp(float start, float end, float frame) { return (1.0f - frame) * start + frame * end; }

// Vector3の各成分を個別にLerpする
Vector3 Easing::Lerp(const Vector3& start, const Vector3& end, float frame) {
	Vector3 result;
	result.x = Lerp(start.x, end.x, frame);
	result.y = Lerp(start.y, end.y, frame);
	result.z = Lerp(start.z, end.z, frame);
	return result;
}

float Easing::EaseIn(float t) { return 1.0f - cosf((t * static_cast<float>(M_PI)) / 2.0f); }

float Easing::EaseOut(float t) { return sinf((t * static_cast<float>(M_PI)) / 2.0f); }

float Easing::EaseInOut(float t) { return -(cosf(static_cast<float>(M_PI) * t) - 1.0f) / 2.0f; }

float Easing::EaseInBack(float t) {
	const float c1 = 1.70158f;
	const float c3 = c1 + 1.0f;
	return c3 * t * t * t - c1 * t * t;
}

float Easing::EaseOutBack(float t) {
	const float c1 = 1.70158f;
	const float c3 = c1 + 1.0f;
	return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
}

float Easing::EaseInQuart(float t) {
	t = std::clamp(t, 0.0f, 1.0f);
	return t * t * t * t;
}

float Easing::EaseOutQuart(float t) {
	t = std::clamp(t, 0.0f, 1.0f);
	return 1.0f - powf(1.0f - t, 4.0f);
}

float Easing::EaseInOutQuart(float t) {
	t = std::clamp(t, 0.0f, 1.0f);
	return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 4.0f) / 2.0f;
}