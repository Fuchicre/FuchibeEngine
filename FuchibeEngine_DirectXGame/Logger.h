#pragma once
#include <ostream>
#include <Windows.h>

class Logger {

public:

	// 出力ストリームを指定するログ出力関数
	static void Log(std::ostream& os, const std::string& message);

	// std::cout へ標準出力する簡易ログ出力関数
	static void Log(const std::string& message);
};