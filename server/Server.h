#include <map>
#include <Windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <sstream>
#include <strsafe.h>
#include <Wingdi.h>
#include <future>
#include <io.h>
#include <fcntl.h>

#include "ServerClass.h"

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

	ServerClass server;

	/* WINDOWS MANAGEMENT */
	// "After creating a window, the creation function returns a window handle that uniquely identifies the window [ndr. HWND]." 
	map<HWND, wstring> windows;

	exception_ptr globalExceptionPtr;
	promise<bool> stopNotificationsThread;
	bool retry;
	int numberRetries;
	
	void WINAPI notificationsManagement();
	static BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam);
	
	/* MIXED: Server and WindowsManagement */
	void sendApplicationToClient(SOCKET clientSocket, HWND hwnd, operation op);		// SERVER
	void receiveCommands();															// WINDOWS MANAGEMENT
	void sendKeystrokesToProgram(vector<UINT> vKeysList);							// WINDOWS MANAGEMENT
	
	static BOOL IsAltTabWindow(HWND hwnd);
	
	/* In più */
	HWINEVENTHOOK g_hook;	// Per funzionalità di cattura eventi

	static void CALLBACK HandleWinEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
	static unsigned CALLBACK hook(void* args);
};

