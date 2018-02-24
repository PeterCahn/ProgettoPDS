#include <Windows.h>
#include <string>

using namespace std;

#pragma once
class Helper
{
public:
	Helper();
	~Helper();

	static HICON getHICONfromHWND(HWND hwnd);
	static long ottieniIcona(BYTE* lpPixels, HWND hwnd);
	static HBITMAP getHBITMAPfromHICON(HICON hIcon);
	static void BitmapInfoErrorExit(LPTSTR lpszFunction);
	static PBITMAPINFO CreateBitmapInfoStruct(HBITMAP hBmp);
	static std::wstring getTitleFromHwnd(HWND hwnd);

};

