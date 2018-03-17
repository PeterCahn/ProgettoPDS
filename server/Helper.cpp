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

HICON CreateAlphaIcon(void)
{
	HDC hMemDC;
	DWORD dwWidth, dwHeight;
	BITMAPV5HEADER bi;
	HBITMAP hBitmap, hOldBitmap;
	void *lpBits;
	DWORD x, y;
	HICON hAlphaIcon = NULL;

	dwWidth = 32;  // width of cursor
	dwHeight = 32;  // height of cursor

	ZeroMemory(&bi, sizeof(BITMAPV5HEADER));
	bi.bV5Size = sizeof(BITMAPV5HEADER);
	bi.bV5Width = dwWidth;
	bi.bV5Height = dwHeight;
	bi.bV5Planes = 1;
	bi.bV5BitCount = 32;
	bi.bV5Compression = BI_BITFIELDS;
	// The following mask specification specifies a supported 32 BPP
	// alpha format for Windows XP.
	bi.bV5RedMask = 0x00FF0000;
	bi.bV5GreenMask = 0x0000FF00;
	bi.bV5BlueMask = 0x000000FF;
	bi.bV5AlphaMask = 0xFF000000;

	HDC hdc;
	hdc = GetDC(NULL);

	// Create the DIB section with an alpha channel.
	hBitmap = CreateDIBSection(hdc, (BITMAPINFO *)&bi, DIB_RGB_COLORS,
		(void **)&lpBits, NULL, (DWORD)0);

	hMemDC = CreateCompatibleDC(hdc);
	ReleaseDC(NULL, hdc);

	// Draw something on the DIB section.
	hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);
	PatBlt(hMemDC, 0, 0, dwWidth, dwHeight, WHITENESS);
	SetTextColor(hMemDC, RGB(0, 0, 0));
	SetBkMode(hMemDC, TRANSPARENT);
	TextOut(hMemDC, 0, 9, L"rgba", 4);
	DWORD *lpdwPixel;
	lpdwPixel = (DWORD *)lpBits;
	for (x = 0; x<dwWidth; x++)
		for (y = 0; y<dwHeight; y++)
		{
			// Clear the alpha bits
			*lpdwPixel &= 0x00FFFFFF;
			// Set the alpha bits to 0x9F (semi-transparent)
			if ((*lpdwPixel & 0x00FFFFFF) == 0)
				*lpdwPixel |= 0xFF000000;
			lpdwPixel++;
		}

	SelectObject(hMemDC, hOldBitmap);
	DeleteDC(hMemDC);

	// Create an empty mask bitmap.
	HBITMAP hMonoBitmap = CreateBitmap(dwWidth, dwHeight, 1, 1, NULL);

	// Set the alpha values for each pixel in the cursor so that
	// the complete cursor is semi-transparent.
	ICONINFO ii;
	ii.fIcon = TRUE;  // Change fIcon to TRUE to create an alpha icon
	ii.xHotspot = 0;
	ii.yHotspot = 0;
	ii.hbmMask = hMonoBitmap;
	ii.hbmColor = hBitmap;

	// Create the alpha cursor with the alpha DIB section.
	hAlphaIcon = CreateIconIndirect(&ii);

	DeleteObject(hBitmap);
	DeleteObject(hMonoBitmap);

	return hAlphaIcon;
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

	HBITMAP hSource = Helper::getHBITMAPfromHICON(Helper::getHICONfromHWND(hwnd));
	//CreateBMPFile(L"c:\\bitmap1.bmp", hSource);

	HBITMAP hMask = CreateBitmapMask(hSource, RGB(0, 0, 0));
	//CreateBMPFile(L"c:\\bitmap2.bmp", hMask);	

	HDC hdc = CreateCompatibleDC(NULL);
	HDC hdcSource = CreateCompatibleDC(hdc);
	
	BITMAPINFO MyBMInfo = { 0 };
	MyBMInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

	// Get the BITMAPINFO structure from the bitmap
	int res;
	if ((res = ::GetDIBits(hdc, hSource, 0, 0, NULL, &MyBMInfo, DIB_RGB_COLORS)) == 0)
	{
		Helper::BitmapInfoErrorExit(L"GetDIBits1()");
	}
	
	/* Ispirato da: http://www.winprog.org/tutorial/transparency.html */
	SelectObject(hdcSource, hMask);
	BitBlt(hdc, 0, 0, MyBMInfo.bmiHeader.biWidth, MyBMInfo.bmiHeader.biHeight, hdcSource, 0, 0, SRCAND);

	SelectObject(hdc, hSource);
	//TransparentBlt(hdc, 0, 0, MyBMInfo.bmiHeader.biWidth, MyBMInfo.bmiHeader.biHeight, hdcSource, 0, 0, MyBMInfo.bmiHeader.biWidth, MyBMInfo.bmiHeader.biHeight, RGB(0, 0, 0));
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
	DeleteObject(hMask);
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

	wstring windowTitle = wstring(title);
	
	if (windowTitle.length() == 0) return wstring(L"explorer.exe");

	return windowTitle;
}

BYTE& Helper::encode(HWND hwnd, u_long& iconLength) {
	ICONINFO icon_info;
	BYTE* pixelsPtr = nullptr;

	if (GetIconInfo(Helper::getHICONfromHWND(hwnd), &icon_info) == FALSE)
		return *pixelsPtr;

	BITMAP bmp;
	if (!icon_info.hbmColor) {
		std::wcerr << "warning: required icon is black/white (not yet implemented)";
		return *pixelsPtr;
	}

	// retrieving the bitmap
	if (GetObject(icon_info.hbmColor, sizeof(bmp), &bmp) <= 0)
		return *pixelsPtr;

	// Allocate memory for the header (should also make space for the color table,
	// but we're not using it, so no need for that)
	BITMAPV5HEADER *hdr = (BITMAPV5HEADER*)std::malloc(sizeof(*hdr));
	hdr->bV5Size = sizeof(BITMAPV5HEADER);
	hdr->bV5Width = bmp.bmWidth;
	hdr->bV5Height = bmp.bmHeight;
	hdr->bV5Planes = 1;
	// 4 bytes per pixel: (hi) ARGB (lo)
	hdr->bV5BitCount = 32; //number of bits that define each pixel
	hdr->bV5Compression = BI_RGB;
	hdr->bV5RedMask = 0x00FF0000;
	hdr->bV5GreenMask = 0x0000FF00;
	hdr->bV5BlueMask = 0x000000FF;
	hdr->bV5AlphaMask = 0xFF000000;
	// will compute this one later
	hdr->bV5SizeImage = 0;
	// this means: don't use/store a palette
	hdr->bV5XPelsPerMeter = 0;
	hdr->bV5YPelsPerMeter = 0;
	hdr->bV5ClrUsed = 0;
	hdr->bV5ClrImportant = 0;

	HDC hdc = GetDC(NULL);

	// Make the device driver calculate the image data size (biSizeImage)
	GetDIBits(hdc, icon_info.hbmColor, 0L, bmp.bmHeight,
		NULL, (BITMAPINFO*)hdr, DIB_RGB_COLORS);

	const size_t scanline_bytes = (((hdr->bV5Width * hdr->bV5BitCount) + 31) & ~31) / 8;
	if (hdr->bV5SizeImage == 0) {
		// Well, that didn't work out. Calculate bV5SizeImage ourselves.
		// The form ((x + n) & ~n) is a trick to round x up to a multiple of n+1.
		// In this case, a multiple of 32 (DWORD-aligned)
		hdr->bV5SizeImage = scanline_bytes * hdr->bV5Height;
	}

	// Make space for the image pixels data
	BYTE *pixels = new BYTE[hdr->bV5SizeImage];
	if (!pixels) {
		std::free(hdr);
		ReleaseDC(NULL, hdc);
		return *pixels;
	}

	iconLength = hdr->bV5SizeImage;

	BOOL got_bits = GetDIBits(hdc, icon_info.hbmColor,
		0L, bmp.bmHeight,
		(LPBYTE)pixels,
		(BITMAPINFO*)hdr,
		DIB_RGB_COLORS);

	ReleaseDC(NULL, hdc);

	if (got_bits == FALSE) {
		// Well, damn.
		std::free(hdr);
		std::free(pixels);
		return *pixels;
	}
	
	return *pixels;
}


BYTE& Helper::getIcon(HWND hwnd, u_long& iconLength)
{
	ICONINFO icon_info;
	BYTE* pixelsPtr = nullptr;

	if (GetIconInfo(Helper::getHICONfromHWND(hwnd), &icon_info) == FALSE)
		return *pixelsPtr;

	BITMAP bmp;
	if (!icon_info.hbmColor) {
		std::wcerr << "warning: required icon is black/white (not yet implemented)";
		return *pixelsPtr;
	}

	// retrieving the bitmap
	if (GetObject(icon_info.hbmColor, sizeof(bmp), &bmp) <= 0)
		return *pixelsPtr;

	// Allocate memory for the header (should also make space for the color table,
	// but we're not using it, so no need for that)
	BITMAPV5HEADER *hdr = (BITMAPV5HEADER*)std::malloc(sizeof(hdr));
	hdr->bV5Size = sizeof(BITMAPV5HEADER);
	hdr->bV5Width = bmp.bmWidth;
	hdr->bV5Height = bmp.bmHeight;
	hdr->bV5Planes = 1;

	// 4 bytes per pixel: (hi) ARGB (lo)
	hdr->bV5BitCount = 32; //number of bits that define each pixel
	hdr->bV5Compression = BI_BITFIELDS;
	hdr->bV5RedMask = 0x00FF0000;
	hdr->bV5GreenMask = 0x0000FF00;
	hdr->bV5BlueMask = 0x000000FF;
	hdr->bV5AlphaMask = 0xFF000000;

	hdr->bV5SizeImage = 0;

	// this means: don't use/store a palette
	hdr->bV5XPelsPerMeter = 0;
	hdr->bV5YPelsPerMeter = 0;
	hdr->bV5ClrUsed = 0;
	hdr->bV5ClrImportant = 0;

	HDC hdc = GetDC(NULL);

	// Make the device driver calculate the image data size (biSizeImage)
	GetDIBits(hdc, icon_info.hbmColor, 0L, bmp.bmHeight,
		NULL, (BITMAPINFO*)hdr, DIB_RGB_COLORS);

	const size_t scanline_bytes = (((hdr->bV5Width * hdr->bV5BitCount) + 31) & ~31) / 8;
	if (hdr->bV5SizeImage == 0) {
		// Well, that didn't work out. Calculate bV5SizeImage ourselves.
		// The form ((x + n) & ~n) is a trick to round x up to a multiple of n+1.
		// In this case, a multiple of 32 (DWORD-aligned)
		hdr->bV5SizeImage = scanline_bytes * hdr->bV5Height;
	}

	// Make space for the image pixels data
	BYTE *pixels = new BYTE[hdr->bV5SizeImage];
	if (!pixels) {
		std::free(hdr);
		ReleaseDC(NULL, hdc);
		return *pixels;
	}

	iconLength = hdr->bV5SizeImage;

	BOOL got_bits = GetDIBits(hdc, icon_info.hbmColor,
		0L, bmp.bmHeight,
		(LPBYTE)pixels,
		(BITMAPINFO*)hdr,
		DIB_RGB_COLORS);

	ReleaseDC(NULL, hdc);

	if (got_bits == FALSE) {
		// Well, damn.
		std::free(hdr);
		std::free(pixels);
		return *pixels;
	}

	return *pixels;
	
}
