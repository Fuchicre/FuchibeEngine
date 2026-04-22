#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <chrono>

// ウィンドウプロシージャ
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {

	// メッセージに応じてゲーム固有の処理を行う
	switch (msg) {

		// ウィンドウが破棄された
	case WM_DESTROY:

		// OSに対して、アプリの終了を伝える
		PostQuitMessage(0);

		return 0;
	}

	// 標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);

}

// ログ出力用の関数
void Log(const std::string& message) {
	OutputDebugStringA(message.c_str());
}

// CoverString関数
std::wstring ConvertString(const std::string& str) {
	if (str.empty()) {
		return std::wstring();
	}

	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0) {
		return std::wstring();
	}
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}

std::string ConvertString(const std::wstring& str) {
	if (str.empty()) {
		return std::string();
	}

	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded == 0) {
		return std::string();
	}
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
}

void Log(std::ostream& os, const std::string& message) {
	os << message << std::endl;
	OutputDebugStringA(message.c_str());
}

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

	WNDCLASS wc{};

	// ウィンドウプロシージャ
	wc.lpfnWndProc = WindowProc;

	// ウィンドウクラス名(なんでも良い)
	wc.lpszClassName = L"FuchibeEngineWindowClass";

	// インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);

	// カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	// ウィンドウクラスを登録する
	RegisterClass(&wc);

	// クライアント領域のサイズ
	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;

	// ウィンドウサイズを表す構造体にクライアント領域を入れる
	RECT wrc = { 0, 0, kClientWidth, kClientHeight };

	// クライアント領域を元に実際のサイズにwrcを変更してもらう
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	//ウィンドウの生成
	HWND hwnd = CreateWindow(
		// 利用するクラス名
		wc.lpszClassName,
		// タイトルバーの文字
		L"FuchibeEngine",
		// よく見るウィンドウスタイル
		WS_OVERLAPPEDWINDOW,
		// 表示するX座標(Windowsに任せる)
		CW_USEDEFAULT,
		// 表示するY座標(WindowsOSに任せる)
		CW_USEDEFAULT,
		// ウィンドウの横幅
		wrc.right - wrc.left,
		// ウィンドウの縦幅
		wrc.bottom - wrc.top,
		// 親ウィンドウハンドル
		nullptr,
		// メニューハンドル
		nullptr,
		// インスタンスハンドル
		wc.hInstance,
		// オプション
		nullptr);

	// ウィンドウを表示する
	ShowWindow(hwnd, SW_SHOW);

	// ログ出力用のディレクトリを作成する
	std::filesystem::create_directory("logs");

	// std::formatによる文字列の組み立て
	Log(std::format("LogFileTest"));

	// 現在時刻を取得 (UTC時刻)
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

	// ログファイルの名前にコンマ何秒はいらないので、削って秒にする
	std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>

		nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);

	// 日本時間(PCの設定時間)に変換
	std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };

	// formatを使って年月日_時分秒の文字列に変換
	std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);

	// 時刻を使ってファイル名を決定
	std::string logFilePath = std::string("logs/") + dateString + ".log";

	// ファイルを作って書き込み準備
	std::ofstream logStream(logFilePath);

	// ループに入る前に1回出す
	Log(logStream, "Game Engine Started.");

	MSG msg{};

	// ウィンドウの×ボタンが押されるまでループ
	while (msg.message != WM_QUIT) {

		// ウィンドウにメッセージが来たら、最優先で処理する
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			// メッセージがある場合は、翻訳して、ウィンドウプロシージャに渡す
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else {
			//ゲームの処理
		}
	}

	// 終了時に記録する
	Log(logStream, "Game Engine Terminated.");

	return 0;
}