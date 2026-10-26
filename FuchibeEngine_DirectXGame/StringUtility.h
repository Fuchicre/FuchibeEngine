#pragma once
#include <string>
#include <Windows.h>

class StringUtility {

public:

	// string を wstring に変換する関数
	static std::wstring ConvertString(const std::string& str);

	// wstring を string に変換する関数
	static std::string ConvertString(const std::wstring& str);
};