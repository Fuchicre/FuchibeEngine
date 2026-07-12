#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <cstdint>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

/// <summary>
/// キーボード入力を一括管理するクラス
/// </summary>
class Input {
private:
	// DirectInputシステムの基盤となるインタフェースへのポインタ
	IDirectInput8* directInput = nullptr;

	// キーボードデバイスを操作するためのインタフェースへのポインタ
	IDirectInputDevice8* keyboard = nullptr;

	// 現在のフレームにおける全キー(256個)の入力状態を保持する配列
	// 各要素の最上位ビット(0x80)が1なら「押されている」、0なら「離されている」を表す
	BYTE key[256] = {};

	// 1つ前のフレームにおける全キー(256個)の入力状態を保持する配列
	// 「押した瞬間（トリガー）」や「離した瞬間」を判定するための比較用に使用する
	BYTE preKey[256] = {};

public:
	/// <summary>
	/// DirectInputおよびキーボードデバイスの初期化処理
	/// </summary>
	/// <param name="hInstance">アプリケーションのインスタンスハンドル</param>
	/// <param name="hwnd">操作対象となるウィンドウのハンドル</param>
	void Initialize(HINSTANCE hInstance, HWND hwnd);

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
	bool IsPress(uint8_t keyCode) const;

	/// <summary>
	/// 指定したキーが現在「離されている状態」かどうかを判定する(キーを離した状態か)
	/// </summary>
	/// <param name="keyCode">キー番号</param>
	/// <returns>離されていれば true、押されていれば false</returns>
	bool IsRelease(uint8_t keyCode) const;

	/// <summary>
	/// 指定したキーが「押された瞬間」かどうかを判定する(キーを押した瞬間か)
	/// </summary>
	/// <param name="keyCode">キー番号</param>
	/// <returns>前フレームで離されていて、現フレームで押されていれば true</returns>
	bool IsTrigger(uint8_t keyCode) const;

	/// <summary>
	/// 指定したキーが「離された瞬間」かどうかを判定する(キーを離した瞬間か)
	/// </summary>
	/// <param name="keyCode">キー番号</param>
	/// <returns>前フレームで押されていて、現フレームで離されていれば true</returns>
	bool IsReleaseTrigger(uint8_t keyCode) const;
};