/* TODO:
- Deallocazione risorse
- Verificare se il thread muore davvero in ogni situazione critica
- Se due finestre hanno lo stesso nome ed una delle due è in focus, vengono entrambe segnate come in focus perchè non sa distinguere quale lo sia veramente, ma poi
solo la prima nella lista del client ha la percentuale che aumenta
- Gestire caso in cui finestra cambia nome (es: "Telegram" con 1 notifica diventa "Telegram(1)") (hint: gestire le app per un ID e non per il loro nome)
- Caso Google Chrome: mostra solo il tab che era aperto al momento dell'avvio del server. Se si cambia tab, rimane il titolo della finestra iniziale.
	=> Provare con EnumChildWindows quando c'è il cambio di focus
- Gestione eccezioni
- Gestione connessione con il client tramite classi C++11

RISOLTI:
- Il reinterpret_cast è corretto? Cioè, è giusto usarlo dov'è usato?
	=> Eliminato perchè le variabili sono ora variabili privte di classe, quindi accessibili senza che vengano passate.
- std::promise globale va bene?
	=> E' una variabile privata della classe Server.
- PASSARE DA THREAD NATIVI WINDOWS A THREAD C++11.
	=> Erano già usati i thread C++11.
- Questione lista applicazioni ed app multithread: a Jure hanno detto che avrebbe dovuto mostrare i thread
	=> Aggiunto "[thread_id] " prima di ogni stampa per mostrare che i thread sono diversi.
- Cos'è la finestra "Program Manager"? - Gestione finestra senza nome (Desktop)
	=> Risolto con "IsAltTabWindow". Mostra solo le finestre veramente visibili, non come "IsWindowVisible"
- Quando il client si chiude o si chiude anche il server (e non dovrebbe farlo) o se sopravvive alla prossima apertura di un client non funziona bene
perchè avvia un nuovo thread notificationsThread senza uccidere il precedente
	=> Risolto tramite il join dei thread
*/
#define WIN32_LEAN_AND_MEAN

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

#include <exception>
#include <typeinfo>
#include <stdexcept>

#include "Server.h"

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
// #pragma comment (lib, "Mswsock.lib")

#define DEFAULT_BUFLEN 1024
#define DEFAULT_PORT  "27015"

using namespace std;

/* Definisce che tipo di notifica è associata alla stringa rappresentante il nome di un finestra da inviare al client */
enum operation {
	OPEN,
	CLOSE,
	FOCUS
};

Server::Server()
{
	/* Inizializza l'exception_ptr per gestire eventuali exception nel background thread */
	globalExceptionPtr = nullptr;

	stopNotificationsThread = promise<bool>();	
}

Server::~Server()
{
	
}

void Server::start()
{
	/* Ottieni porta su cui ascoltare */
	cout << "[" << GetCurrentThreadId() << "] " << "Inserire la porta su cui ascoltare: ";
	cin >> listeningPort;
	while (!cin.good()) {
		cout << "[" << GetCurrentThreadId() << "] " << "Errore nella lettura della porta da ascoltare, reinserirne il valore" << endl;
		cin >> listeningPort;
	}

	while (true) {		

		cout << "[" << GetCurrentThreadId() << "] " << "In attesa della connessione di un client..." << endl;
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
			cout << "[" << GetCurrentThreadId() << "] " << "ERRORE nella creazione del thread 'notificationsThread': " << se.what() << endl;
		}
		catch (const exception &ex)
		{
			cout << "[" << GetCurrentThreadId() << "] " << "Thread 'notificationsThread' terminato con un'eccezione: " << ex.what() << endl;
			/* Riprova a lanciare il thread (?) */
			//thread notificationsThread(&Server::notificationsManagement, this, reinterpret_cast<LPVOID>(&clientSocket));
		}
		
		/* Chiudi la connessione */
		int iResult = shutdown(clientSocket, SD_SEND);
		if (iResult == SOCKET_ERROR) {
			cout << "[" << GetCurrentThreadId() << "] " << "Chiusura della connessione fallita con errore: " << WSAGetLastError() << endl;
		}
		
		/* Cleanup */
		closesocket(clientSocket);
		WSACleanup(); // Terminates use of the Winsock 2 DLL (Ws2_32.dll)
	}

}

BOOL CALLBACK Server::EnumWindowsProc(HWND hWnd, LPARAM lParam)
{
	// Reinterpreta LPARAM lParam come puntatore alla lista dei nomi 
	vector<HWND>* progNames = reinterpret_cast<vector<HWND>*>(lParam);

	// Aggiungi le handle dei programmi aperti al vector<HWND> ricevuto
	if (IsAltTabWindow(hWnd) )
	//if (IsWindowVisible(hWnd))
		progNames->push_back(hWnd);

	return TRUE;
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
		cout << "[" << GetCurrentThreadId() << "] " << "Applicazioni attive:" << endl;
		vector<HWND> currentProgs;

		::EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&currentProgs));
		cout << "[" << GetCurrentThreadId() << "] " << "Programmi aperti: " << endl;
		bool desktopAlreadySent = FALSE;
		for each (HWND hwnd in currentProgs) {
			string windowTitle = getTitleFromHwnd(hwnd);
			if (windowTitle.length() == 0 && !desktopAlreadySent) {
				desktopAlreadySent = TRUE;
				windowTitle = "Desktop";
			}
			else if (windowTitle.length() == 0 && desktopAlreadySent)
				continue;

			cout << "[" << GetCurrentThreadId() << "] " << "- " << windowTitle << endl;
			sendApplicationToClient2(&clientSocket, hwnd, OPEN);
		}

		/* Stampa ed invia finestra col focus */
		HWND currentForegroundHwnd = GetForegroundWindow();
		cout << "[" << GetCurrentThreadId() << "] " << "Applicazione col focus:" << endl;
		cout << "[" << GetCurrentThreadId() << "] " << "- " << getTitleFromHwnd(currentForegroundHwnd) << endl;
		sendApplicationToClient2(&clientSocket, currentForegroundHwnd, FOCUS);

		/* Da qui in poi confronta quello che viene rilevato con quello che si ha */

		/* Controlla lo stato della variabile future: se è stata impostata dal thread principale, è il segnale che questo thread deve terminare */
		future<bool> f = stopNotificationsThread.get_future();
		while (f.wait_for(chrono::seconds(0)) != future_status::ready) {
			// Esegui ogni mezzo secondo
			this_thread::sleep_for(chrono::milliseconds(500));

			/* Variazioni lista programmi */
			vector<HWND> tempProgs;
			::EnumWindows(&Server::EnumWindowsProc, reinterpret_cast<LPARAM>(&tempProgs));

			// Check nuova finestra
			for each(HWND currentHwnd in tempProgs) {
				if (find(currentProgs.begin(), currentProgs.end(), currentHwnd) == currentProgs.end()) {
					// currentProgs non contiene questo programma (quindi è stato aperto ora)
					string windowTitle = getTitleFromHwnd(currentHwnd);
					if (windowTitle.length() != 0) {
						currentProgs.push_back(currentHwnd);
						cout << "[" << GetCurrentThreadId() << "] " << "Nuova finestra aperta!" << endl;
						cout << "[" << GetCurrentThreadId() << "] " << "- " << windowTitle << endl;
						sendApplicationToClient2(&clientSocket, currentHwnd, OPEN);
					}
				}
			}

			// Check chiusura finestra
			vector<HWND> toBeDeleted;
			for each (HWND currentHwnd in currentProgs) {
				if (find(tempProgs.begin(), tempProgs.end(), currentHwnd) == tempProgs.end()) {
					// tempProgs non contiene più currentHwnd
					string windowTitle = getTitleFromHwnd(currentHwnd);
					if (windowTitle.length() != 0) {
						cout << "[" << GetCurrentThreadId() << "] " << "Finestra chiusa!" << endl;
						cout << "[" << GetCurrentThreadId() << "] " << "- " << windowTitle << endl;
						sendApplicationToClient(&clientSocket, currentHwnd, CLOSE);
						toBeDeleted.push_back(currentHwnd);
					}
				}
			}
			for each (HWND deleteThis in toBeDeleted) {
				// Ricava index di deleteThis in currentProgNames per cancellarlo
				auto index = find(currentProgs.begin(), currentProgs.end(), deleteThis);
				currentProgs.erase(index);
			}

			/* Variazioni focus */
			HWND tempForeground = GetForegroundWindow();
			if (tempForeground != currentForegroundHwnd) {
				// Allora il programma che ha il focus è cambiato
				currentForegroundHwnd = tempForeground;
				string windowTitle = getTitleFromHwnd(currentForegroundHwnd);
				if (windowTitle.length() == 0)
					windowTitle = "Desktop";
				cout << "[" << GetCurrentThreadId() << "] " << "Applicazione col focus cambiata! Ora e':" << endl;
				cout << "[" << GetCurrentThreadId() << "] " << "- " << windowTitle << endl;
				sendApplicationToClient2(&clientSocket, currentForegroundHwnd, FOCUS);
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
		std::cout << "WSAStartup() fallita con errore: " << iResult << std::endl;
		return 1;
	}

	// Creazione socket
	listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSocket == INVALID_SOCKET) {
		std::cout << "socket() fallita con errore: " << WSAGetLastError() << std::endl;
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
		std::cout << "bind() fallita con errore: " << WSAGetLastError() << std::endl;
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	// Ascolta per richieste di connessione
	iResult = listen(listenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR) {
		std::cout << "listen() fallita con errore: " << WSAGetLastError() << std::endl;
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	// Accetta la connessione
	clientSocket = accept(listenSocket, NULL, NULL);
	if (clientSocket == INVALID_SOCKET) {
		std::cout << "accept() fallita con errore: " << WSAGetLastError() << std::endl;
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
	std::cout << "[" << GetCurrentThreadId() << "] " << "Connessione stabilita con " << ipstr << ":" << port << std::endl;

	closesocket(listenSocket);

	return clientSocket;
}

string Server::getTitleFromHwnd(HWND hwnd) {
	char title[MAX_PATH];
	GetWindowText(hwnd, title, sizeof(title));
	return string(title);
}

/* Invia il nome della finestra e l'informazione ad esso associata al client
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
void Server::sendApplicationToClient(SOCKET* clientSocket, HWND hwnd, operation op) {

	int moreChars = 20;		// 20 sono i caratteri aggiuntivi alla lunghezza massima del nome di una finestra, dati dal formato del messaggio
	string progName(getTitleFromHwnd(hwnd));
	if (progName.length() == 0)
		progName = "Desktop";

	int bufLen = 0;

	char buf[MAX_PATH + 1000];	
	if (op == OPEN) {
		strcpy_s(buf, MAX_PATH + moreChars, "--OPENP-");
	}
	else if (op == CLOSE) {
		strcpy_s(buf, MAX_PATH + moreChars, "--CLOSE-");
	}
	else {
		strcpy_s(buf, MAX_PATH + moreChars, "--FOCUS-");
	}
	strcat_s(buf, MAX_PATH + moreChars, to_string(progName.length()).c_str());
	strcat_s(buf, MAX_PATH + moreChars, "-");
	strcat_s(buf, MAX_PATH + moreChars, progName.c_str());

	bufLen = strlen(buf);
	//bufLen = sizeof(buf);
	BYTE* lpPixels;

	/* Se è una nuova finestra aggiungere in coda lunghezza file icona e file icona stesso  */
	if (op == OPEN) {
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
			BitmapInfoErrorExit("GetDIBits1()");
		}

		// create the pixel buffer
		lpPixels = new BYTE[MyBMInfo.bmiHeader.biSizeImage];

		MyBMInfo.bmiHeader.biCompression = BI_RGB;

		// Call GetDIBits a second time, this time to (format and) store the actual
		// bitmap data (the "pixels") in the buffer lpPixels		
		if ((res = GetDIBits(hdc, hSource, 0, MyBMInfo.bmiHeader.biHeight, (LPVOID)lpPixels, &MyBMInfo, DIB_RGB_COLORS)) == 0)
		{
			BitmapInfoErrorExit("GetDIBits2()");
		}

		int len = MyBMInfo.bmiHeader.biSizeImage;
		strcat_s(buf, MAX_PATH, "-");
		strcat_s(buf, MAX_PATH, to_string(len).c_str());
		strcat_s(buf, MAX_PATH, "-");

		/* Prepara un nuovo buffer con le ulteriori informazioni da inviare */
		BYTE *buffer = new BYTE[bufLen + to_string(len).length() + 2 + len];
		memcpy(buffer, buf, bufLen + to_string(len).length() + 2);
		memcpy(buffer + bufLen + to_string(len).length() + 2, lpPixels, len);

		send(*clientSocket, (char*)buffer, bufLen + to_string(len).length() + 2 + len, 0);

		DeleteObject(hSource);
		ReleaseDC(NULL, hdcSource);
		delete[] lpPixels;
		delete[] buffer;
		return;
	} else
		send(*clientSocket, buf, bufLen, 0);

	return;
}

void Server::sendApplicationToClient2(SOCKET* clientSocket, HWND hwnd, operation op) {

	string progNameStr(getTitleFromHwnd(hwnd));
	char progName[MAX_PATH];
	u_long progNameLength = progNameStr.length();
	if (progNameLength == 0)
		strcpy_s(progName, "Desktop");
	else
		strcpy_s(progName, progNameStr.c_str());

	int i = 0;
	char msgLengthChars[4];
	u_long msgLength = 0;

	char dimension[7];	// 2 trattini, 4 byte per la dimensione e trattino
	char operation[6];	// 5 byte per l'operazione e trattino + 1
	BYTE* lpPixels = NULL;
	BYTE *finalBuffer = NULL;
	
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
		if ((res = GetDIBits(hdc, hSource, 0, 0, NULL, &MyBMInfo, DIB_RGB_COLORS)) == 0)
		{
			BitmapInfoErrorExit("GetDIBits1()");
		}

		// create the pixel buffer
		long iconLength = MyBMInfo.bmiHeader.biSizeImage;
		lpPixels = new BYTE[iconLength];

		MyBMInfo.bmiHeader.biCompression = BI_RGB;

		// Call GetDIBits a second time, this time to (format and) store the actual
		// bitmap data (the "pixels") in the buffer lpPixels		
		if ((res = GetDIBits(hdc, hSource, 0, MyBMInfo.bmiHeader.biHeight, (LPVOID)lpPixels, &MyBMInfo, DIB_RGB_COLORS)) == 0)
		{
			BitmapInfoErrorExit("GetDIBits2()");
		}

		DeleteObject(hSource);
		ReleaseDC(NULL, hdcSource);

		/* iconLength è la dimensione dell'icona */
		/* Calcola lunghezza totale messaggio e salvala */
		msgLength = 6 + 4 + 1 + 4 + 1 + progNameLength + 1 + 4 + 1 + iconLength;
		u_long netMsgLength = htonl(msgLength);

		memcpy(dimension, "--", 2);
		memcpy(dimension + 2, (void*) &netMsgLength, 4);
		memcpy(dimension + 6, "-", 1);

		/* Salva l'operazione */
		memcpy(operation, "OPENP-", 6);
				
		/* Crea buffer da inviare */
		finalBuffer = new BYTE[7 + msgLength];

		memcpy(finalBuffer, dimension, 7);	// Invia prima la dimensione "--<b1,b2,b3,b4>-" (7 byte)

		memcpy(finalBuffer + 7, operation, 6);	// "<operation>-"	(6 byte)

		memcpy(finalBuffer + 7 + 6, &progNameLength, 4);	// Aggiungi lunghezza progName (4 byte)
		memcpy(finalBuffer + 7 + 6 + 4, "-", 1);	// Aggiungi trattino (1 byte)
		memcpy(finalBuffer + 7 + 6 + 4 + 1, progName, progNameLength);	// <progName>
		memcpy(finalBuffer + 7 + 6 + 4 + 1 + progNameLength, "-", 1);	// Aggiungi trattino (1 byte)

		memcpy(finalBuffer + 7 + 6 + 4 + 1 + progNameLength + 1 , &iconLength, 4);	// Aggiungi dimensione icona (4 byte)
		memcpy(finalBuffer + 7 + 6 + 4 + 1 + progNameLength + 1 + 4, "-", 1);	// Aggiungi trattino (1 byte)
		memcpy(finalBuffer + 7 + 6 + 4 + 1 + progNameLength + 1 + 4 + 1 , lpPixels, iconLength);	// Invia icona
	}
	else if (op == CLOSE) {

		/* Calcola lunghezza totale messaggio e salvala */
		msgLength = 6 + 4 + 1 + 4 + 1 + progNameLength + 1;
		u_long netMsgLength = htonl(msgLength);

		memcpy(dimension, "--", 2);
		memcpy(dimension + 2, (void*) &netMsgLength, 4);
		memcpy(dimension + 6, "-", 1);

		/* Crea operation */
		memcpy(operation, "CLOSE-", 6);

		/* Crea buffer da inviare */
		finalBuffer = new BYTE[7 + msgLength];
		memcpy(finalBuffer, dimension, 7);	// Invia prima la dimensione "--<b1,b2,b3,b4>-" (7 byte)

		memcpy(finalBuffer + 7, operation, 6);	// "<operation>-"	(6 byte)

		memcpy(finalBuffer + 7 + 6, &progNameLength, 4);	// Aggiungi lunghezza progName (4 byte)
		memcpy(finalBuffer + 7 + 6 + 4, "-", 1);	// Aggiungi trattino (1 byte)
		memcpy(finalBuffer + 7 + 6 + 4 + 1, progName, progNameLength);	// <progName>
		memcpy(finalBuffer + 7 + 6 + 4 + 1 + progNameLength, "-", 1);	// Aggiungi trattino (1 byte)
	}
	else {

		/* Calcola lunghezza totale messaggio e salvala */
		msgLength = 6 + 4 + 1 + 4 + 1 + progNameLength + 1;
		u_long netMsgLength = htonl(msgLength);

		memcpy(dimension, "--", 2);
		memcpy(dimension + 2, (void*) &netMsgLength, 4);
		memcpy(dimension + 6, "-", 1);

		memcpy(operation, "FOCUS-", 6);

		/* Crea buffer da inviare */
		finalBuffer = new BYTE[7 + msgLength];
		memcpy(finalBuffer, dimension, 7);	// Invia prima la dimensione "--<b1,b2,b3,b4>-" (7 byte)

		memcpy(finalBuffer + 7, operation, 6);	// "<operation>-"	(6 byte)

		memcpy(finalBuffer + 7 + 6, &progNameLength, 4);	// Aggiungi lunghezza progName (4 byte)
		memcpy(finalBuffer + 7 + 6 + 4, "-", 1);	// Aggiungi trattino (1 byte)
		memcpy(finalBuffer + 7 + 6 + 4 + 1, progName, progNameLength);	// <progName>
		memcpy(finalBuffer + 7 + 6 + 4 + 1 + progNameLength, "-", 1);	// Aggiungi trattino (1 byte)
	}

	int bytesSent = 0;
	//while (bytesSent < msgLength + 7) {
		bytesSent += send(*clientSocket, (char*)finalBuffer + bytesSent, 7 + msgLength - bytesSent, 0);
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
		BitmapInfoErrorExit("GetDIBits1()");
	}

	// create the pixel buffer
	long iconLength = MyBMInfo.bmiHeader.biSizeImage;
	lpPixels = new BYTE[iconLength];

	MyBMInfo.bmiHeader.biCompression = BI_RGB;

	// Call GetDIBits a second time, this time to (format and) store the actual
	// bitmap data (the "pixels") in the buffer lpPixels		
	if ((res = GetDIBits(hdc, hSource, 0, MyBMInfo.bmiHeader.biHeight, (LPVOID)lpPixels, &MyBMInfo, DIB_RGB_COLORS)) == 0)
	{
		BitmapInfoErrorExit("GetDIBits2()");
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
		std::cout << "Impossibile ottenere la PBITMAPINFO" << std::endl;
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
			cout << "[" << GetCurrentThreadId() << "] " << "Chiusura connessione..." << endl << endl;
		else if (iResult < 0) {
			cout << "[" << GetCurrentThreadId() << "] " << "recv() fallita con errore: " << WSAGetLastError() << endl;;
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
			cout << "[" << GetCurrentThreadId() << "] " << "Connessione con il client chiusa." << endl << endl;

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
			std::cout << "Virtual-key ricevute da inviare alla finestra in focus: " << stringaRicevuta << std::endl;
			for each(UINT i in vKeysList)
				std::cout << "- " << i << std::endl;

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

	std::cout << "# of keystrokes to send: " << keystrokes_lenght << std::endl;
	std::cout << "# of keystrokes sent: " << keystrokes_sent << std::endl;
}

/* La funzione MapVirtualKey() traduce virtualKeys in char o "scan codes" in Virtual-keys
* Settandone il primo parametro a MAPVK_VSC_TO_VK_EX, tradurrà il secondo paramentro, che dovrà
* essere uno "scan code", in una Virtual-key.
* Se usassimo KEYEVENTF_UNICODE in dwFlags, dovrebbe essere settato a 0
*/
