#include <Windows.h>
/*
#include <vector>
#include <future>
#include <map>
#include <string>
*/

#include <map>
#include <Windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <strsafe.h>
#include <Wingdi.h>
#include <future>
#include <regex>
#include <io.h>
#include <fcntl.h>

enum operation;

//#pragma once
using namespace std;

class Server
{

public:

	Server();
	~Server();
	void start();

private:
	SOCKET clientSocket;	// Gestisce un client alla volta
	SOCKET listenSocket;	// Il socket del server
	string listeningPort;	// La porta su cui ascoltare connessioni in entrata

	// "After creating a window, the creation function returns a window handle that uniquely identifies the window [ndr. HWND]." 
	map<HWND, wstring> windows;

	exception_ptr globalExceptionPtr;
	promise<bool> stopNotificationsThread;	

	HWINEVENTHOOK g_hook;	// Per funzionalità di cattura eventi


	string leggiPorta();
	SOCKET avviaServer();
	SOCKET acceptConnection();
	DWORD WINAPI notificationsManagement();
	static BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam);

	static void CALLBACK HandleWinEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
	static unsigned CALLBACK hook(void* args);

	void sendApplicationToClient(SOCKET clientSocket, HWND hwnd, operation op);	
	long ottieniIcona(BYTE* lpPixels, HWND hwnd);
	void receiveCommands();
	void sendKeystrokesToProgram(vector<UINT> vKeysList);
	
	static BOOL IsAltTabWindow(HWND hwnd);
	wstring getTitleFromHwnd(HWND hwnd);
	void BitmapInfoErrorExit(LPTSTR lpszFunction);
	HICON getHICONfromHWND(HWND hwnd);
	HBITMAP getHBITMAPfromHICON(HICON hIcon);
	PBITMAPINFO CreateBitmapInfoStruct(HBITMAP hBmp);
	
};

