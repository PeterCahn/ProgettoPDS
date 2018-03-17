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
	static BYTE& ottieniIcona(HWND hwnd, u_long& iconLength);
	static HBITMAP getHBITMAPfromHICON(HICON hIcon);
	static void BitmapInfoErrorExit(LPTSTR lpszFunction);
	static PBITMAPINFO CreateBitmapInfoStruct(HBITMAP hBmp);
	static wstring getTitleFromHwnd(HWND hwnd);
	static HBITMAP CreateBitmapMask(HBITMAP hbmColour, COLORREF crTransparent);

	static BYTE& encode(HWND, u_long&);
	static BYTE& getIcon(HWND, u_long&);

private:
	class InitDC {
		InitDC();
		~InitDC();
	};

};

