#include "FixedFps.h"
#include <thread>

// FPS固定初期化関数
void FixedFps::Initialize() {
	// 現在時間を記録する
	mReferenceTime = std::chrono::steady_clock::now();
}

// FPS固定更新関数
void FixedFps::Update() {

	// 1/60秒ピッタリの時間(1フレーム分の時間)
	const std::chrono::microseconds kMinTime(static_cast<int64_t>(1000000.0f / 60.0f));

	// 1/60秒よりわずかに短い時間
	const std::chrono::microseconds kMinCheckTime(static_cast<int64_t>(1000000.0f / 65.0f));

	// 現在時間を取得する
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

	// 前回記録した時間からの経過時間を計算する
	std::chrono::microseconds elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - mReferenceTime);

	// 1/60秒(よりわずかに短い時間)経っていない場合
	if (elapsed < kMinCheckTime) {
		// 1/60秒経過するまで微小なスリーブを繰り返す
		while (std::chrono::steady_clock::now() - mReferenceTime < kMinTime) {
			// 1マイクロ秒スリーブ
			std::this_thread::sleep_for(std::chrono::microseconds(1));
		}
	}
	// 現在時間を記録する
	mReferenceTime = std::chrono::steady_clock::now();
}