#define UNICODE

#include "Helper.h"

#include <Windows.h>
#include <string>
#include <iostream>
#include <io.h>
#include <strsafe.h>

#pragma comment (lib, "Msimg32.lib")

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

#include "olectl.h"
#pragma comment(lib, "oleaut32.lib") 
HRESULT SaveIcon(HICON hIcon, const wchar_t* path) {
	// Create the IPicture intrface 
	PICTDESC desc = { sizeof(PICTDESC) };
	desc.picType = PICTYPE_ICON;
	desc.icon.hicon = hIcon;
	IPicture* pPicture = 0;
	HRESULT hr = OleCreatePictureIndirect(&desc, IID_IPicture, FALSE, (void**)&pPicture);
	if (FAILED(hr)) return hr;

	// Create a stream and save the image 
	IStream* pStream = 0;
	CreateStreamOnHGlobal(0, TRUE, &pStream);
	LONG cbSize = 0;
	hr = pPicture->SaveAsFile(pStream, TRUE, &cbSize);

	// Write the stream content to the file 
	if (!FAILED(hr)) {
		HGLOBAL hBuf = 0;
		GetHGlobalFromStream(pStream, &hBuf);
		void* buffer = GlobalLock(hBuf);
		HANDLE hFile = CreateFile(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
		if (!hFile) hr = HRESULT_FROM_WIN32(GetLastError());
		else {
			DWORD written = 0;
			WriteFile(hFile, buffer, cbSize, &written, 0);
			CloseHandle(hFile);
		}
		GlobalUnlock(buffer);
	}
	// Cleanup 
	pStream->Release();
	pPicture->Release();
	return hr;
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

/* Crea una maschera in cui i pixel dello sfondo sono bianchi e quelli dell'icona sono neri */
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

PBITMAPINFO CreateBitmapInfoStruct(HBITMAP hBmp)
{
	BITMAP bmp;
	PBITMAPINFO pbmi;
	WORD    cClrBits;

	// Retrieve the bitmap color format, width, and height.  
	GetObject(hBmp, sizeof(BITMAP), (LPSTR)&bmp);

	// Convert the color format to a count of bits.  
	cClrBits = (WORD)(bmp.bmPlanes * bmp.bmBitsPixel);
	if (cClrBits == 1)
		cClrBits = 1;
	else if (cClrBits <= 4)
		cClrBits = 4;
	else if (cClrBits <= 8)
		cClrBits = 8;
	else if (cClrBits <= 16)
		cClrBits = 16;
	else if (cClrBits <= 24)
		cClrBits = 24;
	else cClrBits = 32;

	// Allocate memory for the BITMAPINFO structure. (This structure  
	// contains a BITMAPINFOHEADER structure and an array of RGBQUAD  
	// data structures.)  

	if (cClrBits < 24)
		pbmi = (PBITMAPINFO)LocalAlloc(LPTR,
			sizeof(BITMAPINFOHEADER) +
			sizeof(RGBQUAD) * (1 << cClrBits));

	// There is no RGBQUAD array for these formats: 24-bit-per-pixel or 32-bit-per-pixel 

	else
		pbmi = (PBITMAPINFO)LocalAlloc(LPTR,
			sizeof(BITMAPINFOHEADER));

	// Initialize the fields in the BITMAPINFO structure.  

	pbmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pbmi->bmiHeader.biWidth = bmp.bmWidth;
	pbmi->bmiHeader.biHeight = bmp.bmHeight;
	pbmi->bmiHeader.biPlanes = bmp.bmPlanes;
	pbmi->bmiHeader.biBitCount = bmp.bmBitsPixel;
	if (cClrBits < 24)
		pbmi->bmiHeader.biClrUsed = (1 << cClrBits);

	// If the bitmap is not compressed, set the BI_RGB flag.  
	pbmi->bmiHeader.biCompression = BI_RGB;

	// Compute the number of bytes in the array of color  
	// indices and store the result in biSizeImage.  
	// The width must be DWORD aligned unless the bitmap is RLE 
	// compressed. 
	pbmi->bmiHeader.biSizeImage = ((pbmi->bmiHeader.biWidth * cClrBits + 31) & ~31) / 8
		* pbmi->bmiHeader.biHeight;
	// Set biClrImportant to 0, indicating that all of the  
	// device colors are important.  
	pbmi->bmiHeader.biClrImportant = 0;
	return pbmi;
}

void CreateBMPFile(LPTSTR pszFile, HBITMAP hBMP)
{
	HANDLE hf;                 // file handle  
	BITMAPFILEHEADER hdr;       // bitmap file-header  
	PBITMAPINFOHEADER pbih;     // bitmap info-header  
	LPBYTE lpBits;              // memory pointer  
	DWORD dwTotal;              // total count of bytes  
	DWORD cb;                   // incremental count of bytes  
	BYTE *hp;                   // byte pointer  
	DWORD dwTmp;
	PBITMAPINFO pbi;
	HDC hDC;

	hDC = CreateCompatibleDC(GetWindowDC(GetDesktopWindow()));
	SelectObject(hDC, hBMP);

	pbi = CreateBitmapInfoStruct(hBMP);

	pbih = (PBITMAPINFOHEADER)pbi;
	lpBits = (LPBYTE)GlobalAlloc(GMEM_FIXED, pbih->biSizeImage);

	// Retrieve the color table (RGBQUAD array) and the bits  
	// (array of palette indices) from the DIB.  
	GetDIBits(hDC, hBMP, 0, (WORD)pbih->biHeight, lpBits, pbi,
		DIB_RGB_COLORS);

	// Create the .BMP file.  
	hf = CreateFile(pszFile,
		GENERIC_READ | GENERIC_WRITE,
		(DWORD)0,
		NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		(HANDLE)NULL);
	//assert(hf != INVALID_HANDLE_VALUE);

	hdr.bfType = 0x4d42;        // 0x42 = "B" 0x4d = "M"  
								// Compute the size of the entire file.  
	hdr.bfSize = (DWORD)(sizeof(BITMAPFILEHEADER) +
		pbih->biSize + pbih->biClrUsed
		* sizeof(RGBQUAD) + pbih->biSizeImage);
	hdr.bfReserved1 = 0;
	hdr.bfReserved2 = 0;

	// Compute the offset to the array of color indices.  
	hdr.bfOffBits = (DWORD) sizeof(BITMAPFILEHEADER) +
		pbih->biSize + pbih->biClrUsed
		* sizeof(RGBQUAD);

	// Copy the BITMAPFILEHEADER into the .BMP file.  
	WriteFile(hf, (LPVOID)&hdr, sizeof(BITMAPFILEHEADER),
		(LPDWORD)&dwTmp, NULL);

	// Copy the BITMAPINFOHEADER and RGBQUAD array into the file.  
	WriteFile(hf, (LPVOID)pbih, sizeof(BITMAPINFOHEADER)
		+ pbih->biClrUsed * sizeof(RGBQUAD),
		(LPDWORD)&dwTmp, (NULL));

	// Copy the array of color indices into the .BMP file.  
	dwTotal = cb = pbih->biSizeImage;
	hp = lpBits;
	WriteFile(hf, (LPSTR)hp, (int)cb, (LPDWORD)&dwTmp, NULL);

	// Close the .BMP file.  
	CloseHandle(hf);

	// Free memory.  
	GlobalFree((HGLOBAL)lpBits);
}

BYTE& Helper::ottieniIcona(HWND hwnd, u_long& iconLength) {

	ICONINFO icon_info;
	BYTE* pixelsPtr = nullptr;

	if (GetIconInfo(Helper::getHICONfromHWND(hwnd), &icon_info) == FALSE)
		return *pixelsPtr;

	HBITMAP hSource = icon_info.hbmColor;
	HBITMAP hMask = icon_info.hbmMask;

	HDC hdc = CreateCompatibleDC(NULL);
	HDC hdcSource = CreateCompatibleDC(hdc);

	int width = 32;
	int height = 32;
	
	BITMAPV5HEADER hdr;
	hdr.bV5Size = sizeof(BITMAPV5HEADER);
	hdr.bV5Width = width;
	hdr.bV5Height = height;
	hdr.bV5Planes = 1;
	// 4 bytes per pixel: (hi) ARGB (lo)
	hdr.bV5BitCount = 32; //number of bits that define each pixel
	hdr.bV5Compression = BI_BITFIELDS;
	hdr.bV5RedMask = 0x00FF0000;
	hdr.bV5GreenMask = 0x0000FF00;
	hdr.bV5BlueMask = 0x000000FF;
	hdr.bV5AlphaMask = 0xFF000000;
	// will compute this one later
	hdr.bV5SizeImage = 0;
	// this means: don't use/store a palette
	hdr.bV5XPelsPerMeter = 0;
	hdr.bV5YPelsPerMeter = 0;
	hdr.bV5ClrUsed = 0;
	hdr.bV5ClrImportant = 0;

	//auto m_GDIBitmap = ::CreateDIBSection(hdc, (BITMAPINFO *)&hdr, DIB_RGB_COLORS, (void**)&lpBitmapBits, NULL, (DWORD)0);

	GetDIBits(hdc, icon_info.hbmColor, 0L, height, NULL, (BITMAPINFO*)&hdr, DIB_RGB_COLORS);

	BYTE *pixels = new BYTE[hdr.bV5SizeImage];
	iconLength = hdr.bV5SizeImage;

	GetDIBits(hdc, icon_info.hbmColor, 0L, hdr.bV5Height, (LPBYTE)pixels, (BITMAPINFO*)&hdr, DIB_RGB_COLORS);

	return *pixels;

	//===============

	BITMAPINFO MyBMInfo = { 0 };
	MyBMInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

	// Get the BITMAPINFO structure from the bitmap
	int res;
	if ((res = ::GetDIBits(hdc, hSource, 0, 0, NULL, &MyBMInfo, DIB_RGB_COLORS)) == 0)
	{
		Helper::BitmapInfoErrorExit(L"GetDIBits1()");
	}
	
	/* Ispirato da: http://www.winprog.org/tutorial/transparency.html */
	/*
	SelectObject(hdcSource, hMask);
	BitBlt(hdc, 0, 0, MyBMInfo.bmiHeader.biWidth, MyBMInfo.bmiHeader.biHeight, hdcSource, 0, 0, SRCAND);

	SelectObject(hdc, hSource);
	//TransparentBlt(hdc, 0, 0, MyBMInfo.bmiHeader.biWidth, MyBMInfo.bmiHeader.biHeight, hdcSource, 0, 0, MyBMInfo.bmiHeader.biWidth, MyBMInfo.bmiHeader.biHeight, RGB(0, 0, 0));
	BitBlt(hdc, 0, 0, MyBMInfo.bmiHeader.biWidth, MyBMInfo.bmiHeader.biHeight, hdcSource, 0, 0, SRCPAINT);
	*/
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
	DeleteObject(hMask);
	ReleaseDC(NULL, hdcSource);
	ReleaseDC(NULL, hdc);

	return *lpPixels;
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

	wstring windowTitle = wstring(title);
	
	if (windowTitle.length() == 0) return wstring(L"explorer.exe");

	return windowTitle;
}
