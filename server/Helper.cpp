#pragma once
#define UNICODE

/* Per gestire MFC */
#define _AFXDLL
#include <afx.h>
#include <afxwin.h>
#include <atlbase.h>

#include "Helper.h"

#include <olectl.h>
#pragma comment(lib, "oleaut32.lib")

using namespace std;

Helper::Helper()
{
}

Helper::~Helper()
{
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
		hIcon = reinterpret_cast<HICON>(::LoadIcon(GetModuleHandle(0), MAKEINTRESOURCE(0)));
	}
	// Alternative method. Use OS default icon
	if (hIcon == 0) {
		hIcon = reinterpret_cast<HICON>(::LoadIcon(0, IDI_APPLICATION));
	}
	
	return hIcon;
}

struct ICONDIRENTRY
{
	UCHAR nWidth;
	UCHAR nHeight;
	UCHAR nNumColorsInPalette; // 0 if no palette
	UCHAR nReserved; // should be 0
	WORD nNumColorPlanes; // 0 or 1
	WORD nBitsPerPixel;
	ULONG nDataLength; // length in bytes
	ULONG nOffset; // offset of BMP or PNG data from beginning of file
};
BYTE& Helper::getIconBuffer(HICON hIcon, u_long& szSize)
{
	int nColorBits = 32;
	BYTE* nullBuffer = nullptr;

	if (offsetof(ICONDIRENTRY, nOffset) != 12)
	{
		return *nullBuffer;
	}

	CDC dc;
	dc.Attach(::GetDC(NULL)); // ensure that DC is released when function ends

	// Create an in-memory file 
	CMemFile file;

	// Write header:
	UCHAR icoHeader[6] = { 0, 0, 1, 0, 1, 0 }; // ICO file with 1 image
	file.Write(icoHeader, sizeof(icoHeader));

	// Get information about icon
	ICONINFO iconInfo;
	GetIconInfo(hIcon, &iconInfo);

	BITMAPINFO bmInfo = { 0 };
	bmInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmInfo.bmiHeader.biBitCount = 0;    // don't get the color table     
	if (!GetDIBits(dc, iconInfo.hbmColor, 0, 0, NULL, &bmInfo, DIB_RGB_COLORS))
	{
		return *nullBuffer;
	}

	// Allocate size of bitmap info header plus space for color table:
	int nBmInfoSize = sizeof(BITMAPINFOHEADER);
	if (nColorBits < 24)
	{
		nBmInfoSize += sizeof(RGBQUAD) * (int)(1 << nColorBits);
	}
	CAutoVectorPtr<UCHAR> bitmapInfo;
	bitmapInfo.Allocate(nBmInfoSize);
	BITMAPINFO* pBmInfo = (BITMAPINFO*)(UCHAR*)bitmapInfo;
	memcpy(pBmInfo, &bmInfo, sizeof(BITMAPINFOHEADER));

	// Get bitmap data
	ASSERT(bmInfo.bmiHeader.biSizeImage != 0);
	CAutoVectorPtr<UCHAR> bits;
	bits.Allocate(bmInfo.bmiHeader.biSizeImage);
	pBmInfo->bmiHeader.biBitCount = nColorBits;
	pBmInfo->bmiHeader.biCompression = BI_RGB;
	if (!GetDIBits(dc, iconInfo.hbmColor, 0, bmInfo.bmiHeader.biHeight, (UCHAR*)bits, pBmInfo, DIB_RGB_COLORS))
	{
		return *nullBuffer;
	}

	// Get mask data
	BITMAPINFO maskInfo = { 0 };
	maskInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	maskInfo.bmiHeader.biBitCount = 0;  // don't get the color table     
	if (!GetDIBits(dc, iconInfo.hbmMask, 0, 0, NULL, &maskInfo, DIB_RGB_COLORS))
	{
		return *nullBuffer;
	}
	ASSERT(maskInfo.bmiHeader.biBitCount == 1);
	CAutoVectorPtr<UCHAR> maskBits;
	maskBits.Allocate(maskInfo.bmiHeader.biSizeImage);
	CAutoVectorPtr<UCHAR> maskInfoBytes;
	maskInfoBytes.Allocate(sizeof(BITMAPINFO) + 2 * sizeof(RGBQUAD));
	BITMAPINFO* pMaskInfo = (BITMAPINFO*)(UCHAR*)maskInfoBytes;
	memcpy(pMaskInfo, &maskInfo, sizeof(maskInfo));
	if (!GetDIBits(dc, iconInfo.hbmMask, 0, maskInfo.bmiHeader.biHeight, (UCHAR*)maskBits, pMaskInfo, DIB_RGB_COLORS))
	{
		return *nullBuffer;
	}

	// Write directory entry
	ICONDIRENTRY dir;
	dir.nWidth = (UCHAR)pBmInfo->bmiHeader.biWidth;
	dir.nHeight = (UCHAR)pBmInfo->bmiHeader.biHeight;
	dir.nNumColorsInPalette = (nColorBits == 4 ? 16 : 0);
	dir.nReserved = 0;
	dir.nNumColorPlanes = 0;
	dir.nBitsPerPixel = pBmInfo->bmiHeader.biBitCount;
	dir.nDataLength = pBmInfo->bmiHeader.biSizeImage + pMaskInfo->bmiHeader.biSizeImage + nBmInfoSize;
	dir.nOffset = sizeof(dir) + sizeof(icoHeader);
	file.Write(&dir, sizeof(dir));

	// Write DIB header (including color table)
	int nBitsSize = pBmInfo->bmiHeader.biSizeImage;
	pBmInfo->bmiHeader.biHeight *= 2; // because the header is for both image and mask
	pBmInfo->bmiHeader.biCompression = 0;
	pBmInfo->bmiHeader.biSizeImage += pMaskInfo->bmiHeader.biSizeImage; // because the header is for both image and mask
	file.Write(&pBmInfo->bmiHeader, nBmInfoSize);

	// Write image data
	file.Write((UCHAR*)bits, nBitsSize);

	// Write mask data
	file.Write((UCHAR*)maskBits, pMaskInfo->bmiHeader.biSizeImage);
	szSize = file.GetLength();

	// Get pointer to the buffer
	BYTE* buffer = file.Detach();

	return *buffer;
}

BYTE& Helper::ottieniIcona(HWND hwnd, u_long& iconLength) {
	return getIconBuffer(getHICONfromHWND(hwnd), iconLength);
}

wstring Helper::getTitleFromHwnd(HWND hwnd) {

	TCHAR title[MAX_PATH];
	GetWindowTextW(hwnd, title, sizeof(title));

	wstring windowTitle = wstring(title);

	if (windowTitle.length() == 0) return wstring(L"explorer.exe");

	return windowTitle;
}

