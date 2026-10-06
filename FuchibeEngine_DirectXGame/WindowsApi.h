#pragma once
#include <Windows.h>
#include <cstdint>

class WindowsApi {

	// 静的メンバ関数
public:
	// ウィンドウプロシージャ
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	// メンバ関数
public:

	// 初期化関数
	void Initialize();

	// 更新関数
	void Update();

	// 終了関数
	void Finalize();

	// システムメッセージの処理関数
	bool ProcessMessage();

private:

	// ウィンドウクラスの設定
	WNDCLASS wc{};

	// ウィンドウハンドル
	HWND hwnd = nullptr;

public:

	// クライアント領域のサイズ
	static const int32_t kClientWidth = 1280;
	static const int32_t kClientHeight = 720;

	// getter
	HWND GetHwnd() const { return hwnd; }

	HINSTANCE GetHInstance() const { return wc.hInstance; }

};