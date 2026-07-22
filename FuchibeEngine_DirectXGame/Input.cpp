#include "Input.h"
#include <cassert>

/// <summary>
/// DirectInputおよびキーボードデバイスの初期化処理
/// </summary>
void Input::Initialize(HINSTANCE hInstance, HWND hwnd) {

	//=========================
	// DirectInput全体の初期化
	//=========================

#pragma region DirectInput全体の初期化

	// HRESULT型の変数
	HRESULT hr;

	// DirectInputのAPIを呼び出して、全体の管理オブジェクト(システム)を作成
	hr = DirectInput8Create(
		 // wc.hInstance(アプリケーションのインスタンスハンドル)
		hInstance,
		// 使用するDirectInputのバージョン
		DIRECTINPUT_VERSION,
		// 使用したいインタフェースのID
		IID_IDirectInput8,
		// 生成されたオブジェクトを受け取るポインタへのアドレス
		(void**)&directInput,
		// COMオブジェクトの集約用(通常はnullptr)
		nullptr
	);

	// 初期化が成功したかをチェック。失敗した場合はプログラムを停止させる
	assert(SUCCEEDED(hr));

	// キーボードデバイスの生成
	hr = directInput->CreateDevice(
		// キーボードを指定するための定義済みの識別子
		GUID_SysKeyboard,
		// 生成されたデバイスオブジェクトを受け取るポインタへのアドレス
		&keyboard,
		// COMオブジェクトの集約用(通常はnullptr)
		nullptr
	);
	assert(SUCCEEDED(hr));

	// 入力データ形式のセット //

	// 標準形式
	hr = keyboard->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(hr));

	// 排他制御レベルのセット
	hr = keyboard->SetCooperativeLevel(
		// ウィンドウハンドル
		hwnd,
		// 前面・非排他・Windowsキーを無効
		DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY
	);
	assert(SUCCEEDED(hr));

	// マウスデバイスの生成
	hr = directInput->CreateDevice(
		GUID_SysMouse,
		&mouse,
		nullptr
	);
	assert(SUCCEEDED(hr));

	// 入力データ形式のセット(拡張マウスフォーマット)
	hr = mouse->SetDataFormat(&c_dfDIMouse2);
	assert(SUCCEEDED(hr));

	// 排他制御レベルのセット
	hr = mouse->SetCooperativeLevel(
		hwnd,
		DISCL_FOREGROUND | DISCL_NONEXCLUSIVE
	);
	assert(SUCCEEDED(hr));

#pragma endregion
}

/// <summary>
/// 毎フレームの最初に入力状態を更新する処理
/// </summary>
void Input::Update() {

	// 新しい入力を得る前に、現在の入力状態を「1つ前のフレームの状態(preKey)」へ丸ごとコピーする
	// これにより、前フレームと現フレームの比較(トリガー判定)ができるようになる
	memcpy(preKey, key, sizeof(key));

	// キーボード情報の取得開始
	keyboard->Acquire();

	// 全キーの入力状態を取得する
	keyboard->GetDeviceState(sizeof(key), key);

	// マウスの前フレーム状態を保存
	preMouseState = mouseState;

	// マウス情報の取得開始
	mouse->Acquire();

	// マウスの入力状態を取得する
	mouse->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState);
}

/// <summary>
/// 解放処理
/// </summary>
void Input::Finalize() {

	// マウスデバイスが作られていれば解放する
	if (mouse) {
		mouse->Unacquire();
		mouse->Release();
		mouse = nullptr;
	}

	// キーボードデバイスが作られていれば解放する
	if (keyboard) {
		// キーボードの制御権を明示的に手放す
		keyboard->Unacquire();
		// COMオブジェクトの参照カウントを減らし、メモリを解放する
		keyboard->Release();
		keyboard = nullptr;
	}

	// DirectInputシステムが作られていれば解放する
	if (directInput) {
		// COMオブジェクトの参照カウントを減らし、メモリを解放する
		directInput->Release();
		directInput = nullptr;
	}
}

//=============================================================================
// 各種入力状態の判定関数群
// DirectInputの仕様上、状態データの「最上位ビット(0x80)」が立っていると「押されている」
//=============================================================================

/// <summary>
/// キーを押した状態か
/// </summary>
bool Input::IsPress(uint8_t keyCode) const {
	// 現在のフレームで、該当キーの最上位ビットが1(0x80とAND演算して0以外)なら押されている
	return (key[keyCode] & 0x80) != 0;
}

/// <summary>
/// キーを離した状態か
/// </summary>
bool Input::IsRelease(uint8_t keyCode) const {
	// 現在のフレームで、該当キーの最上位ビットが0(0x80とAND演算して0)なら離されている
	return (key[keyCode] & 0x80) == 0;
}

/// <summary>
/// キーを押した瞬間か(トリガー処理)
/// </summary>
bool Input::IsTrigger(uint8_t keyCode) const {
	// 「前フレームで最上位ビットが0(押されていない)」かつ「現フレームで最上位ビットが1(押されている)」
	// この2つの条件が同時に満たされたとき、まさに「今押された瞬間」と判定する
	return ((preKey[keyCode] & 0x80) == 0) && ((key[keyCode] & 0x80) != 0);
}

/// <summary>
/// キーを離した瞬間か
/// </summary>
bool Input::IsReleaseTrigger(uint8_t keyCode) const {
	// 「前フレームで最上位ビットが1(押されていた)」かつ「現フレームで最上位ビットが0(離された)」
	// この2つの条件が同時に満たされたとき、まさに「今キーが指から離れた瞬間」と判定する
	return ((preKey[keyCode] & 0x80) != 0) && ((key[keyCode] & 0x80) == 0);
}

/// <summary>
/// マウスボタンを押した状態か
/// </summary>
bool Input::IsPressMouse(int32_t buttonNumber) const {
	if (buttonNumber < 0 || buttonNumber >= 8) {
		return false;
	}
	return (mouseState.rgbButtons[buttonNumber] & 0x80) != 0;
}

/// <summary>
/// マウスの移動量およびホイール回転量を取得する
/// </summary>
Input::MouseMove Input::GetMouseMove() const {
	MouseMove move;
	move.lX = mouseState.lX;
	move.lY = mouseState.lY;
	move.lZ = mouseState.lZ;
	return move;
}