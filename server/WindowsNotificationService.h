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

#include "Server.h"

enum operation;

//#pragma once
using namespace std;

class WindowsNotificationService
{

public:

	WindowsNotificationService();
	~WindowsNotificationService();
	void start();
	void stop();

private:

	Server server;
		
	/* "After creating a window, the creation function returns a window handle that uniquely identifies the window [ndr. HWND]." */
	map<HWND, wstring> windows;

	exception_ptr globalExceptionPtr;
	
	/* Per notificare al notificationsThread di terminare il suo lavoro */
	promise<bool> stopNotificationsThread;
	
	/* Per gestire eventuale tentativo di riavvio del notificationThread in caso di eccezione e ricominciare a mandare notifiche al client */
	bool retry;
	int numberRetries;
	
	/* Monitora le finestre aperte e invia notifica al client */
	void WINAPI notificationsManagement();

	/* Ascolta i messaggi in arrivo dal client (notifiche o comandi da inviare alle finestre) */
	void receiveCommands();
	/* Invia comandi alla finestra attualmente in focus */
	void sendKeystrokesToProgram(HWND targetHwnd, vector<INPUT> vKeysList);

	/* Callback per enumerare le finestre attualmente aperte */
	static BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam);
	/* Usato per fare una cernita delle finestre restituite da EnumWindowsProc da aggiungere a 'windows' */
	static BOOL IsAltTabWindow(HWND hwnd);	

	void printMessage(wstring string);
	
	/* In più */
	HWINEVENTHOOK g_hook;	// Per funzionalità di cattura eventi

	static void CALLBACK HandleWinEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
	static unsigned CALLBACK hook(void* args);
};

