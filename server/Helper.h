#include "stdafx.h"
#include <string>

using namespace std;

#pragma once
class Helper
{
public:
	Helper();
	~Helper();

	static BYTE& getHiconBytes(HICON hIcon, u_long& iconLength);

	static HRESULT SaveIconAsFile(HICON hIcon, const wchar_t * path);

	static HICON getHICONfromHWND(HWND hwnd);
	static BYTE& ottieniIcona(HWND hwnd, u_long& iconLength);
	static BYTE& getIconBuffer(HICON hIcon, u_long& iconLength);
	static wstring getTitleFromHwnd(HWND hwnd);	

};

