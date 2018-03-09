#define UNICODE

#include "Helper.h"

#include <Windows.h>
#include <string>
#include <iostream>
#include <io.h>
#include <strsafe.h>

using namespace std;

Helper::Helper()
{
}

Helper::~Helper()
{
}

Helper::InitDC::InitDC() {

}

Helper::InitDC::~InitDC() {

}

HICON Helper::getHICONfromHWND(HWND hwnd) {

	// Get the window icon
	HICON hIcon = (HICON)(::SendMessage(hwnd, WM_GETICON, ICON_BIG, 0));
	if (hIcon == 0) {
		// Alternative method. Get from the window class
		hIcon = reinterpret_cast<HICON>(::GetClassLongPtrW(hwnd, GCLP_HICON));
	}
	// Alternative: get the first icon from the main module 
	if (hIcon == 0) {
		hIcon = ::LoadIcon(GetModuleHandle(0), MAKEINTRESOURCE(0));
	}
	// Alternative method. Use OS default icon
	if (hIcon == 0) {
		hIcon = ::LoadIcon(0, IDI_APPLICATION);
	}
	
	return hIcon;
}

HBITMAP Helper::CreateBitmapMask(HBITMAP hbmColour, COLORREF crTransparent)
{
	HDC hdcMem, hdcMem2;
	HBITMAP hbmMask;
	BITMAP bm;

	// Create monochrome (1 bit) mask bitmap.  

	GetObject(hbmColour, sizeof(BITMAP), &bm);
	hbmMask = CreateBitmap(bm.bmWidth, bm.bmHeight, 1, 1, NULL);

	// Get some HDCs that are compatible with the display driver

	hdcMem = CreateCompatibleDC(0);
	hdcMem2 = CreateCompatibleDC(0);

	SelectObject(hdcMem, hbmColour);
	SelectObject(hdcMem2, hbmMask);

	// Set the background colour of the colour image to the colour
	// you want to be transparent.
	SetBkColor(hdcMem, crTransparent);

	// Copy the bits from the colour image to the B+W mask... everything
	// with the background colour ends up white while everythig else ends up
	// black...Just what we wanted.

	BitBlt(hdcMem2, 0, 0, bm.bmWidth, bm.bmHeight, hdcMem, 0, 0, SRCCOPY);

	// Take our new mask and use it to turn the transparent colour in our
	// original colour image to black so the transparency effect will
	// work right.
	BitBlt(hdcMem, 0, 0, bm.bmWidth, bm.bmHeight, hdcMem2, 0, 0, SRCINVERT);

	// Clean up.

	DeleteDC(hdcMem);
	DeleteDC(hdcMem2);

	return hbmMask;
}

BYTE& Helper::ottieniIcona(HWND hwnd, u_long& iconLength) {

	HBITMAP hSource = Helper::getHBITMAPfromHICON(Helper::getHICONfromHWND(hwnd));
	HBITMAP hMask = CreateBitmapMask(hSource, RGB(0, 0, 0));

	HDC hdc = CreateCompatibleDC(NULL);
	HDC hdcSource = CreateCompatibleDC(hdc);
	
	BITMAPINFO MyBMInfo = { 0 };
	MyBMInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

	// Get the BITMAPINFO structure from the bitmap
	int res;
	if ((res = ::GetDIBits(hdcSource, hSource, 0, 0, NULL, &MyBMInfo, DIB_RGB_COLORS)) == 0)
	{
		Helper::BitmapInfoErrorExit(L"GetDIBits1()");
	}
	
	/* Ispirato da: http://www.winprog.org/tutorial/transparency.html */
	SelectObject(hdcSource, hMask);
	BitBlt(hdc, 0, 0, MyBMInfo.bmiHeader.biWidth, MyBMInfo.bmiHeader.biHeight, hdcSource, 0, 0, SRCAND);

	SelectObject(hdc, hSource);
	BitBlt(hdc, 0, 0, MyBMInfo.bmiHeader.biWidth, MyBMInfo.bmiHeader.biHeight, hdcSource, 0, 0, SRCPAINT);

	// create the pixel buffer
	iconLength = MyBMInfo.bmiHeader.biSizeImage;
	BYTE* lpPixels = new BYTE[iconLength];

	MyBMInfo.bmiHeader.biCompression = BI_RGB;
	MyBMInfo.bmiHeader.biPlanes = 1;

	// Call GetDIBits a second time, this time to (format and) store the actual
	// bitmap data (the "pixels") in the buffer lpPixels		
	if ((res = GetDIBits(hdc, hSource, 0, MyBMInfo.bmiHeader.biHeight, (LPVOID)lpPixels, &MyBMInfo, DIB_RGB_COLORS)) == 0)
	{
		Helper::BitmapInfoErrorExit(L"GetDIBits2()");
	}
	
	DeleteObject(hSource);
	ReleaseDC(NULL, hdcSource);
	ReleaseDC(NULL, hdc);

	return *lpPixels;
}

HBITMAP Helper::getHBITMAPfromHICON(HICON hIcon) {
	int bitmapXdimension = 64;
	int bitmapYdimension = 64;
	HDC hDC = GetDC(NULL);
	HDC hMemDC = CreateCompatibleDC(hDC);
	HBITMAP hMemBmp = CreateCompatibleBitmap(hDC, bitmapXdimension, bitmapYdimension);
	HBITMAP hResultBmp = NULL;
	HGDIOBJ hOrgBMP = SelectObject(hMemDC, hMemBmp);

	DrawIconEx(hMemDC, 0, 0, hIcon, bitmapYdimension, bitmapYdimension, 0, NULL, DI_NORMAL);

	hResultBmp = hMemBmp;
	hMemBmp = NULL;

	SelectObject(hMemDC, hOrgBMP);
	DeleteDC(hMemDC);
	ReleaseDC(NULL, hDC);
	DestroyIcon(hIcon);
	return hResultBmp;
}

void Helper::BitmapInfoErrorExit(LPTSTR lpszFunction)
{
	// Retrieve the system error message for the last-error code

	LPVOID lpMsgBuf;
	LPVOID lpDisplayBuf;
	DWORD dw = GetLastError();

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		dw,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf,
		0, NULL);

	// Display the error message and exit the process

	lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
		//(lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));
		(sizeof((LPCTSTR)lpMsgBuf) + sizeof((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));

	StringCchPrintf((LPTSTR)lpDisplayBuf,
		LocalSize(lpDisplayBuf) / sizeof(TCHAR),
		TEXT("%s failed with error %d: %s"),
		lpszFunction, dw, lpMsgBuf);
	MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);

	LocalFree(lpMsgBuf);
	LocalFree(lpDisplayBuf);
	return;
}

wstring Helper::getTitleFromHwnd(HWND hwnd) {

	TCHAR title[MAX_PATH];
	GetWindowTextW(hwnd, title, sizeof(title));

	return wstring(title);
}

