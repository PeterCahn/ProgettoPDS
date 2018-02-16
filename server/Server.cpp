/* TODO:
- Deallocazione risorse
- Verificare se il thread muore davvero in ogni situazione critica
- Gestione eccezioni
*/
#define WIN32_LEAN_AND_MEAN
#define UNICODE

#include <Windows.h>
#include <Winsock2.h>
#include <ws2tcpip.h>
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

#include <process.h>

#include <oleacc.h>
#pragma comment (lib, "oleacc.lib")

#include <cstdio>

#include <exception>
#include <typeinfo>
#include <stdexcept>

#include "Server.h"

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
// #pragma comment (lib, "Mswsock.lib")

#define DEFAULT_BUFLEN 1024

#define N_BYTE_TRATTINO 1
#define N_BYTE_MSG_LENGTH 4
#define N_BYTE_PROG_NAME_LENGTH 4
#define N_BYTE_OPERATION 5
#define N_BYTE_HWND sizeof(HWND)
#define N_BYTE_ICON_LENGTH 4
#define MSG_LENGTH_SIZE (3*N_BYTE_TRATTINO + N_BYTE_MSG_LENGTH)
#define OPERATION_SIZE (N_BYTE_OPERATION + N_BYTE_TRATTINO)
#define HWND_SIZE (N_BYTE_HWND + N_BYTE_TRATTINO)
#define PROG_NAME_LENGTH (N_BYTE_PROG_NAME_LENGTH + N_BYTE_TRATTINO)
#define ICON_LENGTH_SIZE (N_BYTE_ICON_LENGTH + N_BYTE_TRATTINO)

using namespace std;

/* Definisce che tipo di notifica è associata alla stringa rappresentante il nome di un finestra da inviare al client */
enum operation {
	OPEN,
	CLOSE,
	FOCUS,
	TITLE_CHANGED
};

typedef std::map<HWND, wstring> WinStringMap;

Server::Server()
{
	_setmode(_fileno(stdout), _O_U16TEXT);
	/* Inizializza l'exception_ptr per gestire eventuali exception nel background thread */
	globalExceptionPtr = nullptr;

	windows = map<HWND, wstring>();
	
	stopNotificationsThread = promise<bool>();	
}

Server::~Server()
{
	
}

void Server::start()
{
	/* Ottieni porta su cui ascoltare */
	regex port("10[2-9][4-9]|[2-9][0-9][0-9][0-9]|[1-5][0-9][0-9][0-9][0-9]|6[0-4][0-9][0-9][0-9]|65[0-5][0-9][0-9]|655[0-3][0-9]|6553[0-5]");
	while (true)
	{
		wcout << "[" << GetCurrentThreadId() << "] " << "Inserire la porta su cui ascoltare: ";		
		cin >> listeningPort;

		if (!cin.good())
			wcout << "[" << GetCurrentThreadId() << "] " << "Errore nella lettura. Riprovare.";
		
		else if (!regex_match(listeningPort, port))
			wcout << "[" << GetCurrentThreadId() << "] " << "Intervallo ammesso per il valore della porta: [1024-65535]" << endl;
		else
			break;
	}

	// Tentativo di sganciare un thread e settare una hook function al focus, nome cambiato, etc.
	//thread t(hook, this);

	while (true) {		

		wcout << "[" << GetCurrentThreadId() << "] " << "In attesa della connessione di un client..." << endl;
		clientSocket = acceptConnection();

		/* Crea thread che invia notifiche su cambiamento focus o lista programmi */
		stopNotificationsThread = promise<bool>();	// Reimpostazione di promise prima di creare il thread in modo da averne una nuova, non già soddisfatta, ad ogni ciclo
		try {
			/* Crea thread per gestire le notifiche */
			thread notificationsThread(&Server::notificationsManagement, this);

			/* Thread principale attende eventuali comandi sulla finestra attualmente in focus */
			receiveCommands();	// ritorna quando la connessione con il client è chiusa

			/* Procedura terminazione thread notifiche */
			stopNotificationsThread.set_value(TRUE);
			notificationsThread.join();

			/* Se un'eccezione si è verificata nel background thread viene rilanciata nel main thread */
			if (globalExceptionPtr) rethrow_exception(globalExceptionPtr);
		}
		catch (system_error se) {
			wcout << "[" << GetCurrentThreadId() << "] " << "ERRORE nella creazione del thread 'notificationsThread': " << se.what() << endl;
		}
		catch (const exception &ex)
		{
			wcout << "[" << GetCurrentThreadId() << "] " << "Thread 'notificationsThread' terminato con un'eccezione: " << ex.what() << endl;
			/* Riprova a lanciare il thread (?) */
			//thread notificationsThread(&Server::notificationsManagement, this, reinterpret_cast<LPVOID>(&clientSocket));
		}
		
		/* Chiudi la connessione */
		int iResult = shutdown(clientSocket, SD_SEND);
		if (iResult == SOCKET_ERROR) {
			wcout << "[" << GetCurrentThreadId() << "] " << "Chiusura della connessione fallita con errore: " << WSAGetLastError() << endl;
		}
		
		/* Cleanup */
		closesocket(clientSocket);
		WSACleanup(); // Terminates use of the Winsock 2 DLL (Ws2_32.dll)
	}

}

BOOL CALLBACK Server::EnumWindowsProc(HWND hWnd, LPARAM lParam)
{
	map<HWND, wstring>* windows2 = reinterpret_cast< map<HWND, wstring>* > (lParam);

	DWORD process, thread;
	thread = GetWindowThreadProcessId(hWnd, &process);

	TCHAR title[MAX_PATH];
	GetWindowTextW(hWnd, title, sizeof(title));

	wstring windowTitle = wstring(title);

	// Reinterpreta LPARAM lParam come puntatore alla lista dei nomi 
	//vector<HWND>* progNames = reinterpret_cast<vector<HWND>*>(lParam);

	// Aggiungi le handle dei programmi aperti al vector<HWND> ricevuto
	//if (IsAltTabWindow(hWnd))
//		progNames->push_back(hWnd);

	// Proteggere accesso a variabile condivisa "windows"
	if (IsAltTabWindow(hWnd))
		windows2->insert(pair<HWND,wstring>(hWnd, windowTitle));

	return TRUE;
}

void CALLBACK Server::HandleWinEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
	TCHAR title[MAX_PATH];
	GetWindowTextW(hwnd, title, sizeof(title));

	wstring t = wstring(title);

	vector<HWND> currentProgs;
	EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&currentProgs));
	
	if (find(currentProgs.begin(), currentProgs.end(), hwnd) != currentProgs.end()) {
		// la finestra c'è
		if (event == EVENT_OBJECT_FOCUS)
			wcout << "Focus on: " << hwnd << " " << t << endl;
		else if (event == EVENT_OBJECT_NAMECHANGE)
			wcout << "Name changed: " << hwnd << " " << t << endl;
	}

}

unsigned __stdcall Server::hook(void* args)
{
	HWINEVENTHOOK hHook = NULL;
	vector<HWND> currentProgs;
	EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&currentProgs));

	for each (HWND currentHwnd in currentProgs) {
		if (find(currentProgs.begin(), currentProgs.end(), currentHwnd) != currentProgs.end()) {
			// tempProgs non contiene più currentHwnd
			DWORD ProcessId, ThreadId;
			ThreadId = GetWindowThreadProcessId(currentHwnd, &ProcessId);

			TCHAR title[MAX_PATH];
			GetWindowTextW(currentHwnd, title, sizeof(title));

			wstring t = wstring(title);

			//hHook = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_NAMECHANGE, NULL, HandleWinEvent, 0, 0, WINEVENT_OUTOFCONTEXT);

			//UnhookWinEvent(hHook);
			//CoUninitialize();
		}
	}

	CoInitialize(NULL);
	hHook = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_NAMECHANGE, NULL, HandleWinEvent, 0, 0, WINEVENT_OUTOFCONTEXT);

	MSG msg;
	// Secondo parametro può essere la hwnd della window da cui ricevere i messaggi
	while (GetMessage(&msg, NULL, 0, 0) > 0);

	UnhookWinEvent(hHook);
	CoUninitialize();

	return 0;
}

BOOL Server::IsAltTabWindow(HWND hwnd)
{
	TITLEBARINFO ti;
	HWND hwndTry, hwndWalk = NULL;

	if(!IsWindowVisible(hwnd))
		return FALSE;

	hwndTry = GetAncestor(hwnd, GA_ROOTOWNER);
	while(hwndTry != hwndWalk) 
	{
		hwndWalk = hwndTry;
		hwndTry = GetLastActivePopup(hwndWalk);
		if(IsWindowVisible(hwndTry)) 
			break;
	}
	if(hwndWalk != hwnd)
		return FALSE;

	// the following removes some task tray programs and "Program Manager"
	ti.cbSize = sizeof(ti);
	GetTitleBarInfo(hwnd, &ti);
	if(ti.rgstate[0] & STATE_SYSTEM_INVISIBLE)
		return FALSE;

	return TRUE;
}

DWORD WINAPI Server::notificationsManagement()
{
	try {

		/* Stampa ed invia tutte le finestre */
		wcout << "[" << GetCurrentThreadId() << "] " << "Applicazioni attive:" << endl;
		vector<HWND> currentProgs;

		::EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windows));
		wcout << "[" << GetCurrentThreadId() << "] " << "Programmi aperti: " << endl;
		bool desktopAlreadySent = FALSE;
		for each (pair<HWND, wstring> pair in windows) {
			wstring windowTitle = pair.second;
			//wstring windowTitle = getTitleFromHwnd(hwnd);
			if (windowTitle.length() == 0 && !desktopAlreadySent) {
				desktopAlreadySent = TRUE;
				windowTitle = L"Desktop";
			}
			else if (windowTitle.length() == 0 && desktopAlreadySent)
				continue;

			wcout << "[" << GetCurrentThreadId() << "] " << "- " << windowTitle << endl;
			sendApplicationToClient(clientSocket, pair.first, OPEN);
		}

		/* Stampa ed invia finestra col focus */
		HWND currentForegroundHwnd = GetForegroundWindow();
		wcout << "[" << GetCurrentThreadId() << "] " << "Applicazione col focus:" << endl;
		wcout << "[" << GetCurrentThreadId() << "] " << "- " << getTitleFromHwnd(currentForegroundHwnd) << endl;
		sendApplicationToClient(clientSocket, currentForegroundHwnd, FOCUS);

		/* Da qui in poi confronta quello che viene rilevato con quello che si ha */

		/* Controlla lo stato della variabile future: se è stata impostata dal thread principale, è il segnale che questo thread deve terminare */
		future<bool> f = stopNotificationsThread.get_future();
		while (f.wait_for(chrono::seconds(0)) != future_status::ready) {
			// Esegui ogni mezzo secondo
			this_thread::sleep_for(chrono::milliseconds(500));

			/* Variazioni lista programmi */
			map<HWND, wstring> tempWindows;
			::EnumWindows(&Server::EnumWindowsProc, reinterpret_cast<LPARAM>(&tempWindows));

			// Check nuova finestra
			for each (pair<HWND, wstring> pair in tempWindows) {
				if (windows.find(pair.first) == windows.end()) {
					// 'windows' non contiene questo programma (quindi è stato aperto ora)
					wstring windowTitle = getTitleFromHwnd(pair.first);
					if (windowTitle.length() != 0) {
						// Devo aggiungere la finestra a 'windows'
						windows[pair.first] = windowTitle;						
						wcout << "[" << GetCurrentThreadId() << "] " << "Nuova finestra aperta!" << endl;
						wcout << "[" << GetCurrentThreadId() << "] " << "- " << windowTitle << endl;
						sendApplicationToClient(clientSocket, pair.first, OPEN);
					}
				}
			}

			// Check chiusura finestra
			vector<HWND> toBeDeleted;
			for each (pair<HWND, wstring> pair in windows) {
				if (tempWindows.find(pair.first) == tempWindows.end()) {
					// tempWindows non contiene più pair.first (quindi è stato chiusa)
					wstring windowTitle = getTitleFromHwnd(pair.first);
					if (windowTitle.length() != 0) {
						wcout << "[" << GetCurrentThreadId() << "] " << "Finestra chiusa!" << endl;
						wcout << "[" << GetCurrentThreadId() << "] " << "- " << windowTitle << endl;
						sendApplicationToClient(clientSocket, pair.first, CLOSE);
						toBeDeleted.push_back(pair.first);
					}
				}
			}
			for each(HWND hwnd in toBeDeleted) {
				windows.erase(hwnd);
			}

			/* Check variazione titolo finestre */
			for each (pair<HWND, wstring> pair in windows) {
				if (tempWindows.find(pair.first) != tempWindows.end()) {
					// E' stata trovata la finestra: controlla ora se il titolo è diverso
					wstring previousTitle = windows[pair.first];
					wstring newTitle = tempWindows[pair.first];

					if (previousTitle != newTitle) {
						// Devo aggiungere la finestra a 'windows'
						windows[pair.first] = newTitle;
						wcout << "[" << GetCurrentThreadId() << "] " << "Cambio nome per la finestra: " << endl;
						wcout << "[" << GetCurrentThreadId() << "] " << "\t- " << previousTitle << endl;
						wcout << "[" << GetCurrentThreadId() << "] " << "Ora è: " << endl;
						wcout << "[" << GetCurrentThreadId() << "] " << "- " << newTitle << endl;
						sendApplicationToClient(clientSocket, pair.first, TITLE_CHANGED);
					}
				}
			}

			/* Check variazione focus */
			HWND tempForeground = GetForegroundWindow();
			if (tempForeground != currentForegroundHwnd) {
				// Allora il programma che ha il focus è cambiato
				currentForegroundHwnd = tempForeground;
				wstring windowTitle = getTitleFromHwnd(currentForegroundHwnd);
				if (windowTitle.length() == 0)
					windowTitle = L"Desktop";
				wcout << "[" << GetCurrentThreadId() << "] " << "Applicazione col focus cambiata! Ora e':" << endl;
				wcout << "[" << GetCurrentThreadId() << "] " << "- " << windowTitle << endl;
				sendApplicationToClient(clientSocket, currentForegroundHwnd, FOCUS);
			}
		}
	}
	catch (...)
	{
		//Set the global exception pointer in case of an exception
		globalExceptionPtr = current_exception();
	}

	return 0;
}

SOCKET Server::acceptConnection(void)
{
	WSADATA wsaData;
	int iResult;

	SOCKET listenSocket = INVALID_SOCKET;
	SOCKET clientSocket = INVALID_SOCKET;
		
	int recvbuflen = DEFAULT_BUFLEN;

	// Inizializza Winsock
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		wcout << "WSAStartup() fallita con errore: " << iResult << std::endl;
		return 1;
	}

	// Creazione socket
	listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSocket == INVALID_SOCKET) {
		wcout << "socket() fallita con errore: " << WSAGetLastError() << std::endl;
		WSACleanup();
		return 1;
	}

	// Imposta struct sockaddr_in
	struct sockaddr_in mySockaddr_in;
	mySockaddr_in.sin_addr.s_addr = htonl(INADDR_ANY);
	mySockaddr_in.sin_port = htons(atoi(listeningPort.c_str()));
	mySockaddr_in.sin_family = AF_INET;

	// Associa socket a indirizzo locale
	iResult = ::bind(listenSocket, reinterpret_cast<struct sockaddr*>(&mySockaddr_in), sizeof(mySockaddr_in));
	if (iResult == SOCKET_ERROR) {
		wcout << "bind() fallita con errore: " << WSAGetLastError() << std::endl;
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	// Ascolta per richieste di connessione
	iResult = listen(listenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR) {
		wcout << "listen() fallita con errore: " << WSAGetLastError() << std::endl;
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	// Accetta la connessione
	clientSocket = accept(listenSocket, NULL, NULL);
	if (clientSocket == INVALID_SOCKET) {
		wcout << "accept() fallita con errore: " << WSAGetLastError() << std::endl;
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	struct sockaddr_in clientSockAddr;
	int nameLength = sizeof(clientSockAddr);
	getpeername(clientSocket, reinterpret_cast<struct sockaddr*>(&clientSockAddr), &nameLength);
	int port = ntohs(clientSockAddr.sin_port);
	char ipstr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientSockAddr.sin_addr, ipstr, INET_ADDRSTRLEN);
	wcout << "[" << GetCurrentThreadId() << "] " << "Connessione stabilita con " << ipstr << ":" << port << std::endl;

	closesocket(listenSocket);

	return clientSocket;
}

wstring Server::getTitleFromHwnd(HWND hwnd) {
	TCHAR title[MAX_PATH];
	GetWindowTextW(hwnd, title, sizeof(title));

	return wstring(title);
}

/* 
* Invia il nome della finestra e l'informazione ad esso associata al client
* Il formato del messaggio per le operazioni CLOSE e FOCUS è :
*		--<operazione>-<lunghezza_nome_finestra>-<nomefinestra>
*
* Se l'operazione è OPEN, aggiunge al precedente formato la lunghezza del file contenente l'icona
* seguito dal file stesso secondo il seguente formato:
*		--<operazione>-<lunghezza_nome_finestra>-<nomefinestra>-<lunghezza_file_icona>-<dati_file_icona_bmp>
*
* NB: in notificationsManagement il primo check è quello di nuove finestre (operazione OPEN), i successivi check (FOCUS/CLOSE)
*	   lavorano sulla lista di handle che è stata sicuramente inviata al client e non richiede inviare anche l'icona.
*/
void Server::sendApplicationToClient(SOCKET clientSocket, HWND hwnd, operation op) {
	
	wstring progNameStr(getTitleFromHwnd(hwnd));
	TCHAR progName[MAX_PATH*sizeof(wchar_t)];
	u_long progNameLength = progNameStr.size() * sizeof(wchar_t);
	u_long netProgNameLength = htonl(progNameLength);
	if (progNameLength == 0)
		wcscpy_s(progName, L"Desktop");
	else
		wcscpy_s(progName, progNameStr.c_str());

	int i = 0;
	u_long msgLength = 0;

	char dimension[MSG_LENGTH_SIZE];	// 2 trattini, 4 byte per la dimensione e trattino
	char operation[N_BYTE_OPERATION + N_BYTE_TRATTINO];	// 5 byte per l'operazione e trattino + 1
	BYTE* lpPixels = NULL;
	BYTE* finalBuffer = NULL;
	
	if (op == OPEN) {

		/* Ottieni l'icona */
		HBITMAP hSource = getHBITMAPfromHICON(getHICONfromHWND(hwnd));
		PBITMAPINFO pbi = CreateBitmapInfoStruct(hSource);
		HDC hdc = GetDC(NULL);
		HDC hdcSource = CreateCompatibleDC(hdc);

		BITMAPINFO MyBMInfo = { 0 };
		MyBMInfo.bmiHeader.biSize = sizeof(MyBMInfo.bmiHeader);

		// Get the BITMAPINFO structure from the bitmap
		int res;
		if ((res = ::GetDIBits(hdc, hSource, 0, 0, NULL, &MyBMInfo, DIB_RGB_COLORS)) == 0)
		{
			BitmapInfoErrorExit(L"GetDIBits1()");
		}

		// create the pixel buffer
		long iconLength = MyBMInfo.bmiHeader.biSizeImage;
		lpPixels = new BYTE[iconLength];

		MyBMInfo.bmiHeader.biCompression = BI_RGB;

		// Call GetDIBits a second time, this time to (format and) store the actual
		// bitmap data (the "pixels") in the buffer lpPixels		
		if ((res = GetDIBits(hdc, hSource, 0, MyBMInfo.bmiHeader.biHeight, (LPVOID)lpPixels, &MyBMInfo, DIB_RGB_COLORS)) == 0)
		{
			BitmapInfoErrorExit(L"GetDIBits2()");
		}

		DeleteObject(hSource);
		ReleaseDC(NULL, hdcSource);

		/* iconLength è la dimensione dell'icona */
		/* Calcola lunghezza totale messaggio e salvala */
		msgLength = MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + 
			PROG_NAME_LENGTH + progNameLength + N_BYTE_TRATTINO + ICON_LENGTH_SIZE + iconLength;
		u_long netMsgLength = htonl(msgLength);

		memcpy(dimension, "--", 2*N_BYTE_TRATTINO);
		memcpy(dimension + 2*N_BYTE_TRATTINO, (void*) &netMsgLength, N_BYTE_MSG_LENGTH);
		memcpy(dimension + 2*N_BYTE_TRATTINO + N_BYTE_MSG_LENGTH, "-", N_BYTE_TRATTINO);

		/* Salva l'operazione */
		memcpy(operation, "OPENP-", N_BYTE_OPERATION + N_BYTE_TRATTINO);
				
		/* Crea buffer da inviare */
		finalBuffer = new BYTE[MSG_LENGTH_SIZE + msgLength];

		memcpy(finalBuffer, dimension, MSG_LENGTH_SIZE);	// Invia prima la dimensione "--<b1,b2,b3,b4>-" (7 byte)

		memcpy(finalBuffer + MSG_LENGTH_SIZE, operation, OPERATION_SIZE);	// "<operation>-"	(6 byte)

		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE, &hwnd, N_BYTE_HWND);
		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + N_BYTE_HWND, "-", N_BYTE_TRATTINO);	// Aggiungi trattino (1 byte)

		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE, &netProgNameLength, N_BYTE_PROG_NAME_LENGTH);	// Aggiungi lunghezza progName (4 byte)
		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + N_BYTE_PROG_NAME_LENGTH, "-", N_BYTE_TRATTINO);	// Aggiungi trattino (1 byte)
		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH, progName, progNameLength);	// <progName>
		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH + progNameLength, "-", N_BYTE_TRATTINO);	// Aggiungi trattino (1 byte)

		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH + progNameLength + N_BYTE_TRATTINO , &iconLength, N_BYTE_ICON_LENGTH);	// Aggiungi dimensione icona (4 byte)
		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH + progNameLength + N_BYTE_TRATTINO + N_BYTE_ICON_LENGTH, "-", N_BYTE_TRATTINO);	// Aggiungi trattino (1 byte)
		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH + progNameLength + N_BYTE_TRATTINO + ICON_LENGTH_SIZE, lpPixels, iconLength);	// Aggiungi dati icona
	}
	else if (op == CLOSE) {

		/* Calcola lunghezza totale messaggio e salvala */
		msgLength = MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH + progNameLength;
		u_long netMsgLength = htonl(msgLength);

		memcpy(dimension, "--", 2);
		memcpy(dimension + 2, (void*) &netMsgLength, 4);
		memcpy(dimension + 6, "-", 1);

		/* Crea operation */
		memcpy(operation, "CLOSE-", 6);

		/* Crea buffer da inviare */
		finalBuffer = new BYTE[7 + msgLength];
		
		memcpy(finalBuffer, dimension, MSG_LENGTH_SIZE);	// Invia prima la dimensione "--<b1,b2,b3,b4>-" (7 byte)

		memcpy(finalBuffer + MSG_LENGTH_SIZE, operation, OPERATION_SIZE);	// "<operation>-"	(6 byte)

		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE, &hwnd, N_BYTE_HWND);
		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + N_BYTE_HWND, "-", N_BYTE_TRATTINO);	// Aggiungi trattino (1 byte)

		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE, &netProgNameLength, N_BYTE_PROG_NAME_LENGTH);	// Aggiungi lunghezza progName (4 byte)
		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + N_BYTE_PROG_NAME_LENGTH, "-", N_BYTE_TRATTINO);	// Aggiungi trattino (1 byte)
		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH, progName, progNameLength);	// <progName>
	}
	else if(op == FOCUS){

		/* Calcola lunghezza totale messaggio e salvala */
		msgLength = MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH + progNameLength;
		u_long netMsgLength = htonl(msgLength);

		memcpy(dimension, "--", 2);
		memcpy(dimension + 2, (void*) &netMsgLength, 4);
		memcpy(dimension + 6, "-", 1);

		memcpy(operation, "FOCUS-", 6);

		/* Crea buffer da inviare */
		finalBuffer = new BYTE[MSG_LENGTH_SIZE + msgLength];

		memcpy(finalBuffer, dimension, MSG_LENGTH_SIZE);	// Invia prima la dimensione "--<b1,b2,b3,b4>-" (7 byte)

		memcpy(finalBuffer + MSG_LENGTH_SIZE, operation, OPERATION_SIZE);	// "<operation>-"	(6 byte)

		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE, &hwnd, N_BYTE_HWND);
		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + N_BYTE_HWND, "-", N_BYTE_TRATTINO);	// Aggiungi trattino (1 byte)

		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE, &netProgNameLength, N_BYTE_PROG_NAME_LENGTH);	// Aggiungi lunghezza progName (4 byte)
		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + N_BYTE_PROG_NAME_LENGTH, "-", N_BYTE_TRATTINO);	// Aggiungi trattino (1 byte)
		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH, progName, progNameLength);	// <progName>

	}
	else if (op == TITLE_CHANGED) {
		/* Calcola lunghezza totale messaggio e salvala */
		msgLength = MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH + progNameLength;
		u_long netMsgLength = htonl(msgLength);

		memcpy(dimension, "--", 2);
		memcpy(dimension + 2, (void*)&netMsgLength, 4);
		memcpy(dimension + 6, "-", 1);

		memcpy(operation, "TTCHA-", 6);

		/* Crea buffer da inviare */
		finalBuffer = new BYTE[MSG_LENGTH_SIZE + msgLength];

		memcpy(finalBuffer, dimension, MSG_LENGTH_SIZE);	// Invia prima la dimensione "--<b1,b2,b3,b4>-" (7 byte)

		memcpy(finalBuffer + MSG_LENGTH_SIZE, operation, OPERATION_SIZE);	// "<operation>-"	(6 byte)

		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE, &hwnd, N_BYTE_HWND);
		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + N_BYTE_HWND, "-", N_BYTE_TRATTINO);	// Aggiungi trattino (1 byte)

		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE, &netProgNameLength, N_BYTE_PROG_NAME_LENGTH);	// Aggiungi lunghezza progName (4 byte)
		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + N_BYTE_PROG_NAME_LENGTH, "-", N_BYTE_TRATTINO);	// Aggiungi trattino (1 byte)
		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH, progName, progNameLength);	// <progName>
	}

	int bytesSent = 0;
	//while (bytesSent < msgLength + 7) {
		bytesSent += send(clientSocket, (char*)finalBuffer + bytesSent, 7 + msgLength - bytesSent, 0);
	//}
	
	return;
}

long Server::ottieniIcona(BYTE* lpPixels, HWND hwnd) {

	/* Ottieni l'icona */
	HBITMAP hSource = getHBITMAPfromHICON(getHICONfromHWND(hwnd));
	PBITMAPINFO pbi = CreateBitmapInfoStruct(hSource);
	HDC hdc = GetDC(NULL);
	HDC hdcSource = CreateCompatibleDC(hdc);

	BITMAPINFO MyBMInfo = { 0 };
	MyBMInfo.bmiHeader.biSize = sizeof(MyBMInfo.bmiHeader);

	// Get the BITMAPINFO structure from the bitmap
	int res;
	if ((res = GetDIBits(hdc, hSource, 0, 0, NULL, &MyBMInfo, DIB_RGB_COLORS)) == 0)
	{
		BitmapInfoErrorExit(L"GetDIBits1()");
	}

	// create the pixel buffer
	long iconLength = MyBMInfo.bmiHeader.biSizeImage;
	lpPixels = new BYTE[iconLength];

	MyBMInfo.bmiHeader.biCompression = BI_RGB;

	// Call GetDIBits a second time, this time to (format and) store the actual
	// bitmap data (the "pixels") in the buffer lpPixels		
	if ((res = GetDIBits(hdc, hSource, 0, MyBMInfo.bmiHeader.biHeight, (LPVOID)lpPixels, &MyBMInfo, DIB_RGB_COLORS)) == 0)
	{
		BitmapInfoErrorExit(L"GetDIBits2()");
	}

	DeleteObject(hSource);
	ReleaseDC(NULL, hdcSource);

	return iconLength;
}

HICON Server::getHICONfromHWND(HWND hwnd) {

	// Get the window icon
	HICON hIcon = (HICON)(::SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0));
	if (hIcon == 0) {
		// Alternative method. Get from the window class
		hIcon = reinterpret_cast<HICON>(::GetClassLongPtrW(hwnd, GCLP_HICONSM));
	}
	// Alternative: get the first icon from the main module 
	if (hIcon == 0) {
		hIcon = ::LoadIcon(GetModuleHandleW(0), MAKEINTRESOURCE(0));
	}
	// Alternative method. Use OS default icon
	if (hIcon == 0) {
		hIcon = ::LoadIcon(0, IDI_APPLICATION);
	}

	return hIcon;
	//return (HICON)GetClassLong(hwnd, GCL_HICON);
}

HBITMAP Server::getHBITMAPfromHICON(HICON hIcon) {
	int bitmapXdimension = 256;
	int bitmapYdimension = 256;
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

PBITMAPINFO Server::CreateBitmapInfoStruct(HBITMAP hBmp)
{
	BITMAP bmp;
	PBITMAPINFO pbmi;
	WORD    cClrBits;

	// Retrieve the bitmap color format, width, and height.  
	if (!GetObject(hBmp, sizeof(BITMAP), (LPVOID*)&bmp)) {
		wcout << "Impossibile ottenere la PBITMAPINFO" << std::endl;
		return nullptr;
	}

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

void Server::receiveCommands() {
	// Ricevi finchè il client non chiude la connessione
	char recvbuf[DEFAULT_BUFLEN*sizeof(char)];
	char sendBuf[DEFAULT_BUFLEN*sizeof(char)];

	int iResult;
	do {
		iResult = recv(clientSocket, recvbuf, DEFAULT_BUFLEN, 0);
		if (iResult == 0)
			wcout << "[" << GetCurrentThreadId() << "] " << "Chiusura connessione..." << endl << endl;
		else if (iResult < 0) {
			wcout << "[" << GetCurrentThreadId() << "] " << "recv() fallita con errore: " << WSAGetLastError() << endl;;
			closesocket(clientSocket);
			WSACleanup();
			return;
		} 
		/* Se ricevo "--CLOSE-" il client vuole disconnettersi: invio la conferma ed esco */
		else if (strcmp(recvbuf, "--CLOSE-")) {			

			u_long msgLength = 5;
			u_long netMsgLength = htonl(msgLength);

			memcpy(sendBuf, "--", 2);
			memcpy(sendBuf + 2, (void*)&netMsgLength, 4);
			memcpy(sendBuf + 6, "-", 1);

			memcpy(sendBuf + 7, "OKCLO-", 5);
			
			send(clientSocket, sendBuf, 12, 0);
			wcout << "[" << GetCurrentThreadId() << "] " << "Connessione con il client chiusa." << endl << endl;

			return;
		}
		else {
			// Questa stringa contiene i virtual-keys ricevuti separati da '+'
			string stringaRicevuta(recvbuf);

			// Converti la stringa in una lista di virtual-keyes
			stringstream sstream(stringaRicevuta);
			string virtualKey;
			vector<UINT> vKeysList;
			while (getline(sstream, virtualKey, '+'))	// ogni virtual-key è seprata dalle altre dal carattere '+'
			{
				UINT vKey;
				sscanf_s(virtualKey.c_str(), "%u", &vKey);
				vKeysList.push_back(vKey);
			}

			// TODO: rimuovere dopo debug (?)
			// Stampa codici virtual-key ricevute
			wcout << "Virtual-key ricevute da inviare alla finestra in focus: " << endl; // << stringaRicevuta << std::endl;
			for each(UINT i in vKeysList)
				wcout << "- " << i << std::endl;

			// Invia keystrokes all'applicazione in focus
			sendKeystrokesToProgram(vKeysList);
		}

	} while (iResult > 0);

}

void Server::BitmapInfoErrorExit(LPTSTR lpszFunction)
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

void Server::sendKeystrokesToProgram(std::vector<UINT> vKeysList)
{
	INPUT *keystroke;
	int i, keystrokes_lenght, keystrokes_sent;
	HWND progHandle;

	// Ricava l'handle alla finestra verso cui indirizzare il keystroke
	progHandle = GetForegroundWindow();

	// Riempi vettore di keystroke da inviare
	keystrokes_lenght = vKeysList.size();
	keystroke = new INPUT[keystrokes_lenght * 2];	// *2 perchè abbiamo pressione e rilascio dei tasti
													// Pressione dei tasti
	for (i = 0; i < keystrokes_lenght; ++i) {
		keystroke[i * 2].type = INPUT_KEYBOARD;		// Definisce il tipo di input, che può essere INPUT_HARDWARE, INPUT_KEYBOARD o INPUT_MOUSE
													// Una volta definito il tipo di input come INPUT_KEYBOARD, si usa la sotto-struttura .ki per inserire le informazioni sull'input
		keystroke[i * 2].ki.wVk = vKeysList[i];		// Virtual-key code dell'input.																						  
		keystroke[i * 2].ki.wScan = 0;				// Se usassimo KEYEVENTF_UNICODE in dwFlags, wScan specificherebbe il carettere UNICODE da inviare alla finestra in focus
		keystroke[i * 2].ki.dwFlags = 0;			// Eventuali informazioni addizionali sull'evento
		keystroke[i * 2].ki.time = 0;				// Timestamp dell'evento. Settandolo a 0, il SO lo imposta in automatico
		keystroke[i * 2].ki.dwExtraInfo = GetMessageExtraInfo();	// Valore addizionale associato al keystroke
	}
	// Rilascio dei tasti
	for (i = 0; i < keystrokes_lenght; ++i) {
		keystroke[i * 2 + 1].type = INPUT_KEYBOARD;
		keystroke[i * 2 + 1].ki.wVk = vKeysList[i];
		keystroke[i * 2 + 1].ki.wScan;
		keystroke[i * 2 + 1].ki.dwFlags = KEYEVENTF_KEYUP;
		keystroke[i * 2 + 1].ki.time = 0;
		keystroke[i * 2 + 1].ki.dwExtraInfo = GetMessageExtraInfo();
	}

	//Send the keystrokes.
	keystrokes_sent = SendInput((UINT)keystrokes_lenght, keystroke, sizeof(*keystroke));
	delete[] keystroke;

	wcout << "# of keystrokes to send: " << keystrokes_lenght << std::endl;
	wcout << "# of keystrokes sent: " << keystrokes_sent << std::endl;
}

/* La funzione MapVirtualKey() traduce virtualKeys in char o "scan codes" in Virtual-keys
* Settandone il primo parametro a MAPVK_VSC_TO_VK_EX, tradurrà il secondo paramentro, che dovrà
* essere uno "scan code", in una Virtual-key.
* Se usassimo KEYEVENTF_UNICODE in dwFlags, dovrebbe essere settato a 0
*/
