#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <cstdint>
#include <Windows.h>
#include <wrl.h>
#include "WindowsApi.h"

/// <summary>
/// キーボードおよびマウス入力を一括管理するクラス
/// </summary>
class Input {
public:
	// マウスの移動量およびホイール回転量を保持する構造体
	struct MouseMove {
		// X軸移動量
		long lX;
		// Y軸移動量
		long lY;
		// ホイール回転量
		long lZ;
	};

public:

	// namespace省略
	template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
	/// <summary>
	/// DirectInputおよびキーボード・マウスデバイスの初期化処理
	/// </summary>
	/// <param name="hInstance">アプリケーションのインスタンスハンドル</param>
	/// <param name="hwnd">操作対象となるウィンドウのハンドル</param>
	void Initialize(WindowsApi* windowsApi);

	/// <summary>
	/// 毎フレームの最初に入力状態を更新する処理(元の取得処理を内包)
	/// </summary>
	void Update();

	/// <summary>
	/// 解放処理
	/// </summary>
	void Finalize();

	/// <summary>
	/// 指定したキーが現在「押されている状態」かどうかを判定する(キーを押した状態か)
	/// </summary>
	/// <param name="keyCode">DIK_SPACE や DIK_0 などのキー番号</param>
	/// <returns>押されていれば true、離されていれば false</returns>
	bool IsPressKey(uint8_t keyCode) const;

	/// <summary>
	/// 指定したキーが現在「離されている状態」かどうかを判定する(キーを離した状態か)
	/// </summary>
	/// <param name="keyCode">キー番号</param>
	/// <returns>離されていれば true、押されていれば false</returns>
	bool IsReleaseKey(uint8_t keyCode) const;

	/// <summary>
	/// 指定したキーが「押された瞬間」かどうかを判定する(キーを押した瞬間か)
	/// </summary>
	/// <param name="keyCode">キー番号</param>
	/// <returns>前フレームで離されていて、現フレームで押されていれば true</returns>
	bool IsTriggerKey(uint8_t keyCode) const;

	/// <summary>
	/// 指定したキーが「離された瞬間」かどうかを判定する(キーを離した瞬間か)
	/// </summary>
	/// <param name="keyCode">キー番号</param>
	/// <returns>前フレームで押されていて、現フレームで離されていれば true</returns>
	bool IsReleaseTriggerKey(uint8_t keyCode) const;

	/// <summary>
	/// 指定したマウスボタンが「押されている状態」かどうかを判定する
	/// </summary>
	/// <param name="buttonNumber">0:左ボタン, 1:右ボタン, 2:中ボタン(ホイールクリック)</param>
	/// <returns>押されていれば true、離されていれば false</returns>
	bool IsPressMouse(int32_t buttonNumber) const;

	/// <summary>
	/// 指定したマウスボタンが「押された瞬間」かどうかを判定する
	/// </summary>
	bool IsTriggerMouse(int32_t buttonNumber) const;

	/// <summary>
	/// マウスの移動量およびホイール回転量を取得する
	/// </summary>
	MouseMove GetMouseMove() const;

private:
	// DirectInputシステムの基盤となるインタフェースへのポインタ
	ComPtr<IDirectInput8> directInput = nullptr;

	// キーボードデバイスを操作するためのインタフェースへのポインタ
	ComPtr<IDirectInputDevice8> keyboard = nullptr;

	// 現在のフレームにおける全キー(256個)の入力状態を保持する配列
	// 各要素の最上位ビット(0x80)が1なら「押されている」、0なら「離されている」を表す
	BYTE key[256] = {};

	// 1つ前のフレームにおける全キー(256個)の入力状態を保持する配列
	// 「押した瞬間（トリガー）」や「離した瞬間」を判定するための比較用に使用する
	BYTE preKey[256] = {};

	// マウスデバイスを操作するためのインタフェースへのポインタ
	ComPtr<IDirectInputDevice8> mouse = nullptr;

	// 現在のフレームにおけるマウスの入力状態
	DIMOUSESTATE2 mouseState = {};

	// 1つ前のフレームにおけるマウスの入力状態
	DIMOUSESTATE2 preMouseState = {};

	// WindowsAPI
	WindowsApi* windowsApi = nullptr;
};