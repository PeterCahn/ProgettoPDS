/* TODO:
- Deallocazione risorse
- Verificare se il thread muore davvero in ogni situazione critica
- Gestione eccezioni
- Raramente (in condizioni non ben specificate) il server moriva. Non è chiaro se dopo aver definito il
	server da cui disconnettersi e su cui inviare e ricevere il segnale di close, il problema non si ripete più.
	(NB: Rientra nella verifica delle eccezioni, se succede qualcosa, reagisci in modo che il client non si blocchi)
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

// Gestione eventi windows
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

#define MAX_RETRIES 3

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
	numberRetries = 0;
	retry = false;

	windows = map<HWND, wstring>();
	
	stopNotificationsThread = promise<bool>();	
}

Server::~Server()
{
	
}

void Server::start()
{
	/* Ottieni porta su cui ascoltare e accetta prima connessione */	
	listeningPort = leggiPorta();

	/* Avvia il server controllando che il socket ricevuto sia corretto per poter procedere */
	while (true)
	{		
		/* Tentativo di avviare il server con la porta letta */
		listenSocket = avviaServer();
		if (listenSocket != INVALID_SOCKET)	// server avviato: break
			break;
		else
			listeningPort = leggiPorta();	// problema con l'avvio del server: rileggi porta
	}

	/* Tentativo di sganciare un thread per raccogliere i messaggi nella coda degli eventi windows delle finestre monitorate.
		Gestione eventi windows (semplificato ed adattato), preso spunto da qui: http://www.cplusplus.com/forum/windows/58791/
		NB: Togli commento dalla prossima riga per ascoltare gli eventi.
			Le righe successive non verranno eseguite perchè la hook esegue un ciclo while continuo (vedi funzione hook)
	*/	 
	//thread t(hook, this);

	while (true) {

		/* Aspetta nuove connessioni in arrivo e si rimette in attesa se non è possibile accettare la connessione dal client */
		wcout << "[" << GetCurrentThreadId() << "] " << "In attesa della connessione di un client..." << endl;		
		while (true) {
			clientSocket = acceptConnection();
			if (clientSocket != INVALID_SOCKET)
				break;
		}

		
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
			WSACleanup(); // Terminates use of the Winsock 2 DLL (Ws2_32.dll)
		}
		catch (exception &ex)
		{
			wcout << "[" << GetCurrentThreadId() << "] " << "Thread 'notificationsThread' terminato con un'eccezione: " << ex.what() << endl;
			/* Riprova a lanciare il thread (?) */
			//wcout << "[" << GetCurrentThreadId() << "] " << "Tentativo riavvio 'notificationsThread' sul client" << endl;				
				
		}
		/* Cleanup */
		closesocket(clientSocket);		
	}

	closesocket(listenSocket);
	WSACleanup(); // Terminates use of the Winsock 2 DLL (Ws2_32.dll)
}

/* Acquisisce la porta verificando che sia un numero tra 1024 e 65535 */
string Server::leggiPorta()
{
	/* Ottieni porta su cui ascoltare */
	string porta;
	regex portRegex("102[4-9]|10[3-9][0-9]|[2-9][0-9][0-9][0-9]|[1-5][0-9][0-9][0-9][0-9]|6[0-4][0-9][0-9][0-9]|65[0-5][0-9][0-9]|655[0-3][0-9]|6553[0-5]");
	while (true)
	{
		wcout << "[" << GetCurrentThreadId() << "] " << "Inserire la porta su cui ascoltare: ";
		cin >> porta;

		if (!wcin.good()) {
			wcout << "[" << GetCurrentThreadId() << "] " << "Errore nella lettura. Riprovare.";
			return NULL;
		}

		else if (!regex_match(porta, portRegex))
			wcout << "[" << GetCurrentThreadId() << "] " << "Intervallo ammesso per il valore della porta: [1024-65535]" << endl;
		else
			break;
	}

	return porta;
}

/* Avvia il server settando la listeningPort del Server */
SOCKET Server::avviaServer()
{
	WSADATA wsaData;
	int iResult;

	listenSocket = INVALID_SOCKET;

	int recvbuflen = DEFAULT_BUFLEN;

	// Inizializza Winsock
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		wcout << "[" << GetCurrentThreadId() << "] " << "WSAStartup() fallita con errore: " << iResult << std::endl;
		return INVALID_SOCKET;
	}

	// Creazione socket
	listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSocket == INVALID_SOCKET) {
		wcout << "[" << GetCurrentThreadId() << "] " << "socket() fallita con errore: " << WSAGetLastError() << std::endl;
		WSACleanup();
		return INVALID_SOCKET;
	}

	// Imposta struct sockaddr_in
	struct sockaddr_in mySockaddr_in;
	mySockaddr_in.sin_addr.s_addr = htonl(INADDR_ANY);
	mySockaddr_in.sin_port = htons(atoi(listeningPort.c_str()));
	mySockaddr_in.sin_family = AF_INET;

	// Associa socket a indirizzo locale
	iResult = ::bind(listenSocket, reinterpret_cast<struct sockaddr*>(&mySockaddr_in), sizeof(mySockaddr_in));
	if (iResult == SOCKET_ERROR) {
		int errorCode = WSAGetLastError();

		wcout << "[" << GetCurrentThreadId() << "] " << "bind() fallita con errore: " << WSAGetLastError() << std::endl;
		if (errorCode == WSAEADDRINUSE)
			wcout << "[" << GetCurrentThreadId() << "] " << "Porta " << atoi(listeningPort.c_str()) << " già in uso. Scegliere un'altra porta." << endl;

		closesocket(listenSocket);
		WSACleanup();
		return INVALID_SOCKET;
	}

	// Ascolta per richieste di connessione
	iResult = listen(listenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR) {
		wcout << "[" << GetCurrentThreadId() << "] " << "listen() fallita con errore: " << WSAGetLastError() << std::endl;
		closesocket(listenSocket);
		WSACleanup();
		return INVALID_SOCKET;
	}

	return listenSocket;
}

/* Attende una connessione in entrata da un client */
SOCKET Server::acceptConnection(void)
{
	int iResult = 0;

	SOCKET newClientSocket;

	// Accetta la connessione
	newClientSocket = accept(listenSocket, NULL, NULL);
	if (newClientSocket == INVALID_SOCKET) {
		wcout << "[" << GetCurrentThreadId() << "] " << "accept() fallita con errore: " << WSAGetLastError() << std::endl;
		return INVALID_SOCKET;
	}

	struct sockaddr_in clientSockAddr;
	int nameLength = sizeof(clientSockAddr);
	getpeername(newClientSocket, reinterpret_cast<struct sockaddr*>(&clientSockAddr), &nameLength);
	int port = ntohs(clientSockAddr.sin_port);
	char ipstr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientSockAddr.sin_addr, ipstr, INET_ADDRSTRLEN);
	wcout << "[" << GetCurrentThreadId() << "] " << "Connessione stabilita con " << ipstr << ":" << port << std::endl;
	
	return newClientSocket;
}

BOOL CALLBACK Server::EnumWindowsProc(HWND hWnd, LPARAM lParam)
{
	map<HWND, wstring>* windows2 = reinterpret_cast< map<HWND, wstring>* > (lParam);

	//DWORD process, thread;
	//thread = GetWindowThreadProcessId(hWnd, &process);

	TCHAR title[MAX_PATH];
	GetWindowTextW(hWnd, title, sizeof(title));

	wstring windowTitle = wstring(title);
		
	// Proteggere accesso a variabile condivisa "windows"
	if (IsAltTabWindow(hWnd))
		windows2->insert(pair<HWND,wstring>(hWnd, windowTitle));

	return TRUE;
}

/* Questa funzione viene passata alla SetWinEventHook nella funzione hook per gestire gli eventi */
void CALLBACK Server::HandleWinEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
	TCHAR title[MAX_PATH];
	GetWindowTextW(hwnd, title, sizeof(title));

	wstring t = wstring(title);

	map<HWND, wstring> tempWindows;
	EnumWindows(&Server::EnumWindowsProc, reinterpret_cast<LPARAM>(&tempWindows));
	
	if (tempWindows.find(hwnd) != tempWindows.end()) {
		// la finestra c'è
		GetWindowTextW(hwnd, title, sizeof(title));
		wstring t = wstring(title);

		/* Non considero alcuni eventi che non ci interessano o che vengono chiamati di continuo */
		if (
			event == EVENT_OBJECT_LOCATIONCHANGE
			|| event == EVENT_OBJECT_REORDER
			|| (event > 0x4001 && event < 0x4007) 
			|| event == EVENT_OBJECT_VALUECHANGE 
			|| event == 16385
			|| (event == 8 || event == 9)	// click giù e click su
			)
			return;
		
		if( event == EVENT_OBJECT_FOCUS || event == EVENT_SYSTEM_FOREGROUND)
			wcout << "New focus: [" << hwnd << "] " << t << endl;
		else if(event == EVENT_OBJECT_NAMECHANGE)
			wcout << "Name changed: [" << hwnd << "] " << t << endl;
		else if(event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_UNCLOAKED || event == EVENT_OBJECT_SHOW)
			wcout << "Finestra aperta: [" << hwnd << "] " << t << endl;
		else if(event == EVENT_OBJECT_CLOAKED || event == EVENT_OBJECT_DESTROY || event == EVENT_OBJECT_STATECHANGE)
			wcout << "Finestra chiusa: [" << hwnd << "] " << t << endl;
		
		/*
		switch (event)
		{
			// La finestra in foreground è cambiata
		case EVENT_SYSTEM_FOREGROUND:
			wcout << "Change foreground: [" << hwnd << "] " << t << endl;
			break;
		case EVENT_OBJECT_FOCUS:
			// La finestra ha ricevuto il focus
			wcout << "New focus (focus): [" << hwnd << "] " << t << endl;
			break;
			// Il nome di una finestra è cambiato
		case EVENT_OBJECT_NAMECHANGE:
			wcout << "Name changed: [" << hwnd << "] " << t << endl;
			break;
			// Una nuova finestra è stata creata (avviene solo in alcune occasioni)
		case EVENT_OBJECT_CREATE:
			// Funziona quando viene chiusa la calcolatrice
			wcout << "Finestra aperta (create): [" << hwnd << "] " << t << endl;
			break;				
		case EVENT_OBJECT_SHOW:
			wcout << "New focus (show): [" << hwnd << "] " << t << endl;
			break;				
		case EVENT_OBJECT_CLOAKED:
			wcout << "Finestra chiusa (cloaked): [" << hwnd << "] " << t << endl;
			break;				
		case EVENT_OBJECT_UNCLOAKED:
			// funziona quando viene aperta la calcolatrice
			wcout << "Finestra aperta (uncloaked): [" << hwnd << "] " << t << endl;
			break;
		case EVENT_OBJECT_DESTROY:	// chiamato quasi mai
			wcout << "Finestra chiusa (destroyed): [" << hwnd << "] " << t << endl;
			break;
		case EVENT_OBJECT_STATECHANGE:
			// l'evento non è propriamente per la chiusura, ma per cambio di stato di un oggetto qualsiasi
			wcout << "Finestra chiusa (state change): [" << hwnd << "] " << t << endl;
			break;
		default:
			wcout << "Event happened (" << event << "): [" << hwnd << "] " << t << endl;
			break;				
		}
		*/
	}
	
}

unsigned __stdcall Server::hook(void* args)
{
	HWINEVENTHOOK hHook = NULL;

	CoInitialize(NULL);
	/* Primo e secondo parametro sono i limiti inferiore e superiore degli eventi da ascoltare.
		wWINEVENT_OUTOFCONTEXT per ascoltare tutti i thread e non solo quelli creati da questa applicazione */
	hHook = SetWinEventHook(0x1, 0x80FF, NULL, HandleWinEvent, 0, 0, WINEVENT_OUTOFCONTEXT);

	MSG msg;
	// Secondo parametro può essere la hwnd della window da cui ricevere i messaggi
	//while (GetMessage(&msg, NULL, 0, 0) > 0)

	map<HWND, wstring> tempWindows;
	while (true)
	{
		EnumWindows(&Server::EnumWindowsProc, reinterpret_cast<LPARAM>(&tempWindows));

		for each (pair<HWND, wstring> pair in tempWindows) 
		{
			if (tempWindows.find(pair.first) != tempWindows.end()) 
			{
				// la finestra c'è
				
				if (GetMessage(&msg, pair.first, 0, 0)) {
					//TranslateMessage(&msg);
					//DispatchMessage(&msg);
				}				
				
			}
		}
	}

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

	TCHAR title[MAX_PATH];
	GetWindowTextW(hwnd, title, sizeof(title));

	wstring windowTitle = wstring(title);
	if (windowTitle.length() == 0)
		return FALSE;

	/* For each visible window, walk up its owner chain until you find the root owner.
	 * Then walk back down the visible last active popup chain until you find a visible window.
	 * If you're back to where you're started, then put the window in the Alt + Tab list.
	 * 
	 ** TODO: Prova questo prima o poi
	 while ((hwndTry = GetLastActivePopup(hwndWalk)) != hwndTry) {
	 if (IsWindowVisible(hwndTry)) break;
	 hwndWalk = hwndTry;
	 }
	 return hwndWalk == hwnd;
	 */
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

	if (GetWindowLong(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
		return FALSE;

	return TRUE;
}

void WINAPI Server::notificationsManagement()
{
	try {

		/* Stampa ed invia tutte le finestre con flag OPEN */
		wcout << "[" << GetCurrentThreadId() << "] " << "Applicazioni attive:" << endl;
		EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windows));
		wcout << "[" << GetCurrentThreadId() << "] " << "Programmi aperti: " << endl;
		
		for each (pair<HWND, wstring> pair in windows) {
			wstring windowTitle = pair.second;
			wcout << "[" << GetCurrentThreadId() << "] " << "- " << windowTitle << endl;
			sendApplicationToClient(clientSocket, pair.first, OPEN);
		}

		/* Stampa ed invia finestra col focus con flag FOCUS */
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
					wstring windowTitle = pair.second;
					
					// Devo aggiungere la finestra a 'windows'
					windows[pair.first] = windowTitle;
					wcout << "[" << GetCurrentThreadId() << "] " << "Nuova finestra aperta!" << endl;
					wcout << "[" << GetCurrentThreadId() << "] " << "- " << windowTitle << endl;
					sendApplicationToClient(clientSocket, pair.first, OPEN);
					
				}
			}

			// Check chiusura finestra
			vector<HWND> toBeDeleted;
			for each (pair<HWND, wstring> pair in windows) {
				if (tempWindows.find(pair.first) == tempWindows.end()) {
					// tempWindows non contiene più pair.first (quindi è stata chiusa)
					wstring windowTitle = pair.second;
					
					wcout << "[" << GetCurrentThreadId() << "] " << "Finestra chiusa!" << endl;
					wcout << "[" << GetCurrentThreadId() << "] " << "- " << windowTitle << endl;
					sendApplicationToClient(clientSocket, pair.first, CLOSE);
					toBeDeleted.push_back(pair.first);
					
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
						wcout << "[" << GetCurrentThreadId() << "] " << "La finestra ora in focus è: " << endl;
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
				
				wcout << "[" << GetCurrentThreadId() << "] " << "Applicazione col focus cambiata! Ora e':" << endl;
				wcout << "[" << GetCurrentThreadId() << "] " << "- " << windowTitle << endl;
				sendApplicationToClient(clientSocket, currentForegroundHwnd, FOCUS);
			}

			windows = tempWindows;
		}
	}
	catch (future_error &fe)
	{
		// cosa fare?
	}
	catch (const std::exception &exc)
	{
		// catch anything thrown within try block that derives from std::exception
		wcout << exc.what();
	}
	catch (...)
	{
		//Sleep(5000);
		//Set the global exception pointer in case of an exception
		globalExceptionPtr = current_exception();

		char sendBuf[12 * sizeof(char)];
		u_long msgLength = 5;
		u_long netMsgLength = htonl(msgLength);

		memcpy(sendBuf, "--", 2);
		memcpy(sendBuf + 2, (void*)&netMsgLength, 4);
		memcpy(sendBuf + 6, "-", 1);

		memcpy(sendBuf + 7, "ERRCL-", 5);

		send(clientSocket, sendBuf, 12, 0);		
		//wcout << "[" << GetCurrentThreadId() << "] " << "Connessione con il client chiusa." << endl << endl;

	}

	//return 0;
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
	
	/* Ottieni il nome dalla map 'windows' */
	wstring progNameStr(windows[hwnd]);

	/* Prepara variabile TCHAR per essere copiata sul buffer di invio */
	TCHAR progName[MAX_PATH*sizeof(wchar_t)];
	ZeroMemory(progName, MAX_PATH * sizeof(wchar_t));

	/* Copia in progName la stringa ottenuta */
	wcscpy_s(progName, progNameStr.c_str());

	/* Prepara il valore della lunghezza del nome del programma in Network Byte Order */
	u_long progNameLength = progNameStr.size() * sizeof(wchar_t);
	u_long netProgNameLength = htonl(progNameLength);

	int i = 0;
	u_long msgLength = 0;

	char dimension[MSG_LENGTH_SIZE];	// 2 trattini, 4 byte per la dimensione e trattino
	char operation[N_BYTE_OPERATION + N_BYTE_TRATTINO];	// 5 byte per l'operazione e trattino + 1
	BYTE* lpPixels = NULL;
	BYTE* finalBuffer = NULL;
	
	if (op == OPEN) {

		//throw exception("eccezione voluta");

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
	int offset = 0;
	int remaining = MSG_LENGTH_SIZE + msgLength;
	while (remaining > 0)
	{
		bytesSent = send(clientSocket, (char*)finalBuffer, remaining, offset);
		remaining -= bytesSent;
		offset += bytesSent;
	}
	
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
			int errorCode = WSAGetLastError();
			if (errorCode == WSAECONNRESET) {
				wcout << "[" << GetCurrentThreadId() << "] " << "Connessione chiusa dal client." << endl;
			}else
				wcout << "[" << GetCurrentThreadId() << "] " << "recv() fallita con errore: " << WSAGetLastError() << endl;
			closesocket(clientSocket);
			return;
		}

		/* Se ricevo "--CLOSE-" il client vuole disconnettersi: invio la conferma ed esco */
		if (strcmp(recvbuf, "--CLOSE-") == 0) {			

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
	for (i = 0; i < keystrokes_lenght; i++) {
		keystroke[i].type = INPUT_KEYBOARD;		// Definisce il tipo di input, che può essere INPUT_HARDWARE, INPUT_KEYBOARD o INPUT_MOUSE
												// Una volta definito il tipo di input come INPUT_KEYBOARD, si usa la sotto-struttura .ki per inserire le informazioni sull'input
		keystroke[i].ki.wVk = vKeysList[i];		// Virtual-key code dell'input.																						  
		keystroke[i].ki.wScan = 0;				// Se usassimo KEYEVENTF_UNICODE in dwFlags, wScan specificherebbe il carettere UNICODE da inviare alla finestra in focus
		keystroke[i].ki.dwFlags = 0;			// Eventuali informazioni addizionali sull'evento
		keystroke[i].ki.time = 0;				// Timestamp dell'evento. Settandolo a 0, il SO lo imposta in automatico
		keystroke[i].ki.dwExtraInfo = 0;		// Valore addizionale associato al keystroke, servirebbe ad indicare che il tasto premuto fa parte del tastierino numerico
	}
	// Rilascio dei tasti
	for (i = keystrokes_lenght; i < keystrokes_lenght * 2; i++) {
		keystroke[i].type = INPUT_KEYBOARD;
		keystroke[i].ki.wVk = vKeysList[i - keystrokes_lenght];
		keystroke[i].ki.wScan;
		keystroke[i].ki.dwFlags = KEYEVENTF_KEYUP;
		keystroke[i].ki.time = 0;
		keystroke[i].ki.dwExtraInfo = 0;
	}

	//Send the keystrokes.
	keystrokes_sent = SendInput((UINT)keystrokes_lenght*2, keystroke, sizeof(*keystroke));
	delete[] keystroke;

	wcout << "# of keystrokes to send to the window: " << keystrokes_lenght << endl;
	wcout << "# of keystrokes sent to the window: " << keystrokes_sent << endl;
}

/* La funzione MapVirtualKey() traduce virtualKeys in char o "scan codes" in Virtual-keys
* Settandone il primo parametro a MAPVK_VSC_TO_VK_EX, tradurrà il secondo paramentro, che dovrà
* essere uno "scan code", in una Virtual-key.
* Se usassimo KEYEVENTF_UNICODE in dwFlags, dovrebbe essere settato a 0
*/
