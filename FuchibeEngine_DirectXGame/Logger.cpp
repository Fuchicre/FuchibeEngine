#include "Logger.h"
#include <iostream>

// 出力ストリームを指定するログ出力関数
void Logger::Log(std::ostream& os, const std::string& message) {
	os << message << std::endl;
	OutputDebugStringA(message.c_str());
}

// std::cout へ標準出力する簡易ログ出力関数
void Logger::Log(const std::string& message) {
	Log(std::cout, message);
}