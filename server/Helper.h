#include <Windows.h>
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
	BYTE & ottieniIcona_OLD(HWND hwnd, u_long & iconLength);
	static void getHBITMAPfromHICON(HICON hIcon, HBITMAP& hSource, HBITMAP& hMask);
	static void BitmapInfoErrorExit(LPTSTR lpszFunction);
	static PBITMAPINFO CreateBitmapInfoStruct(HBITMAP hBmp);
	static wstring getTitleFromHwnd(HWND hwnd);
	static HBITMAP CreateBitmapMask(HBITMAP hbmColour, COLORREF crTransparent);

private:
	class InitDC {
		InitDC();
		~InitDC();
	};

};

