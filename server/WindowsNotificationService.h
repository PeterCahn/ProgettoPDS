#pragma once
#include "Server.h"
#include "VirtualDesktop.h"

#include <map>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <future>

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
	
	/* Per notificare al notificationsThread di terminare il suo lavoro */
	promise<bool> stopNotificationsThread;
	
	/* Per gestire eventuale tentativo di riavvio del notificationThread in caso di eccezione e ricominciare a mandare notifiche al client */
	bool retry;
	int numberRetries;
	
	/* Monitora le finestre aperte e invia notifica al client */
	void notificationsManagement();

	/* Ascolta i messaggi in arrivo dal client (notifiche o comandi da inviare alle finestre) */
	void receiveCommands();
	/* Controlla se la virtual key è una extended key*/
	bool WindowsNotificationService::isExtendedKey(WORD virtualKey);
	/* Invia comandi alla finestra attualmente in focus */
	void sendKeystrokesToProgram(HWND targetHwnd, vector<INPUT> vKeysList);

	/* Callback per enumerare le finestre attualmente aperte */
	static BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam);
	/* Usato per fare una cernita delle finestre restituite da EnumWindowsProc da aggiungere a 'windows' */
	static BOOL IsAltTabWindow(HWND hwnd);	

	/* Indica se la finestra a cui corrisponde l'HWND è effettivamente presente su uno dei virtual desktops */
	static bool hasVirtualDesktop(HWND hwnd);

	void printMessage(wstring string);
};

