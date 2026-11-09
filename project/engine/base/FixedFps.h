#pragma once
#include <chrono>

/// <summary>
/// FPS固定処理を管理するクラス
/// </summary>
class FixedFps {
public:
	// FPS固定初期化関数
	void Initialize();

	// FPS固定更新関数
	void Update();

private:
	// 記録時間(FPS固定用)
	std::chrono::steady_clock::time_point mReferenceTime;
};