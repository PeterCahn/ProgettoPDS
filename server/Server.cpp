
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
#include "Helper.h"

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
// #pragma comment (lib, "Mswsock.lib")

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

/* Definisce che tipo di notifica è associata alla stringa rappresentante il nome di un finestra da inviare al client */
enum operation {
	OPEN,
	CLOSE,
	FOCUS,
	TITLE_CHANGED
};

Server::Server()
{
	WSADATA wsaData;

	// Inizializza Winsock
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		//wcout << "[" << GetCurrentThreadId() << "] " << "WSAStartup() fallita con errore: " << iResult << endl;
		wcout << "[" << GetCurrentThreadId() << "] " << "ServerClass non inizializzata correttamente." << endl;
		return;
	}
}


Server::~Server()
{
	/* Se non è stato ancora chiuso il socket, chiudilo */
	if(validServer())
		closesocket(listeningSocket);
	// Terminates use of the Winsock 2 DLL (Ws2_32.dll)
	WSACleanup();
}

/* Acquisisce la porta verificando che sia un numero tra 1024 e 65535 */
void Server::leggiPorta()
{
	/* Ottieni porta su cui ascoltare */
	string porta;
	regex portRegex("102[4-9]|10[3-9][0-9]|[2-9][0-9][0-9][0-9]|[1-5][0-9][0-9][0-9][0-9]|6[0-4][0-9][0-9][0-9]|65[0-5][0-9][0-9]|655[0-3][0-9]|6553[0-5]");
	while (true)
	{
		cin >> porta;

		if (!cin.good()) {
			wcout << "[" << GetCurrentThreadId() << "] " << "Errore nella lettura. Riprovare.";
			/* TODO: Tentativo di recupuperare 'cin" */
			return;
		}
		else if (!regex_match(porta, portRegex)) {
			wcout << "[" << GetCurrentThreadId() << "] " << "Intervallo ammesso per il valore della porta: [1024-65535]" << endl;
		}
		else
			break;
	}
	listeningPort = porta;

	return;
}

/* Avvia il server settando la listeningPort */
void Server::avviaServer()
{
	int iResult;
	
	listeningSocket = INVALID_SOCKET;

	while (true) 
	{
		wcout << "[" << GetCurrentThreadId() << "] " << "Inserire la porta su cui ascoltare: ";
		leggiPorta();
		if (!listeningPort.compare("")) {
			/* Tentativo di recupuperare 'cin" */
			continue;
		}

		// Creazione socket
		listeningSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listeningSocket == INVALID_SOCKET) {
			wcout << "[" << GetCurrentThreadId() << "] " << "socket() fallita con errore: " << WSAGetLastError() << endl;
			WSACleanup();
			listeningSocket = INVALID_SOCKET;
			continue;
		}

		// Imposta struct sockaddr_in
		struct sockaddr_in mySockaddr_in;
		mySockaddr_in.sin_addr.s_addr = htonl(INADDR_ANY);
		mySockaddr_in.sin_port = htons(atoi(listeningPort.c_str()));
		mySockaddr_in.sin_family = AF_INET;

		// Associa socket a indirizzo locale
		iResult = ::bind(listeningSocket, reinterpret_cast<struct sockaddr*>(&mySockaddr_in), sizeof(mySockaddr_in));
		if (iResult == SOCKET_ERROR) {
			int errorCode = WSAGetLastError();

			wcout << "[" << GetCurrentThreadId() << "] " << "bind() fallita con errore: " << WSAGetLastError() << endl;
			if (errorCode == WSAEADDRINUSE)
				wcout << "[" << GetCurrentThreadId() << "] " << "Porta " << atoi(listeningPort.c_str()) << " già in uso. Scegliere un'altra porta." << endl;

			closesocket(listeningSocket);
			WSACleanup();
			listeningSocket = INVALID_SOCKET;
			continue;
		}

		// Ascolta per richieste di connessione
		iResult = listen(listeningSocket, SOMAXCONN);
		if (iResult == SOCKET_ERROR) {
			wcout << "[" << GetCurrentThreadId() << "] " << "listen() fallita con errore: " << WSAGetLastError() << endl;
			closesocket(listeningSocket);
			WSACleanup();
			listeningSocket = INVALID_SOCKET;
			continue;
		}

		if (validServer())	// server avviato con successo: break
			break;
	}

	return;
}

/* Attende una connessione in entrata da un client e setta il clientSocket */
void Server::acceptConnection(void)
{
	int iResult = 0;

	SOCKET newClientSocket;

	// Accetta la connessione
	newClientSocket = accept(listeningSocket, NULL, NULL);
	if (newClientSocket == INVALID_SOCKET) {
		wcout << "[" << GetCurrentThreadId() << "] " << "accept() fallita con errore: " << WSAGetLastError() << endl;
		newClientSocket = INVALID_SOCKET;
		return;
	}

	struct sockaddr_in clientSockAddr;
	int nameLength = sizeof(clientSockAddr);
	getpeername(newClientSocket, reinterpret_cast<struct sockaddr*>(&clientSockAddr), &nameLength);
	int port = ntohs(clientSockAddr.sin_port);
	char ipstr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientSockAddr.sin_addr, ipstr, INET_ADDRSTRLEN);
	wcout << "[" << GetCurrentThreadId() << "] " << "Connessione stabilita con " << ipstr << ":" << port << std::endl;

	clientSocket = newClientSocket;

	return;
}

void Server::chiudiConnessioneClient()
{
	closesocket(clientSocket);
}

void Server::arrestaServer()
{
	closesocket(listeningSocket);
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
void Server::sendNotificationToClient(HWND hwnd, wstring title, operation op) {
	
	/* Prepara variabile TCHAR per essere copiata sul buffer di invio */
	TCHAR progName[MAX_PATH * sizeof(wchar_t)];
	ZeroMemory(progName, MAX_PATH * sizeof(wchar_t));

	/* Copia in progName la stringa ottenuta */
	wcscpy_s(progName, title.c_str());

	/* Prepara il valore della lunghezza del nome del programma in Network Byte Order */
	u_long progNameLength = title.size() * sizeof(wchar_t);
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
		HBITMAP hSource = Helper::getHBITMAPfromHICON(Helper::getHICONfromHWND(hwnd));
		PBITMAPINFO pbi = Helper::CreateBitmapInfoStruct(hSource);
		HDC hdc = GetDC(NULL);
		HDC hdcSource = CreateCompatibleDC(hdc);

		BITMAPINFO MyBMInfo = { 0 };
		MyBMInfo.bmiHeader.biSize = sizeof(MyBMInfo.bmiHeader);

		// Get the BITMAPINFO structure from the bitmap
		int res;
		if ((res = ::GetDIBits(hdc, hSource, 0, 0, NULL, &MyBMInfo, DIB_RGB_COLORS)) == 0)
		{
			//Helper::BitmapInfoErrorExit(L"GetDIBits1()");
		}

		// create the pixel buffer
		long iconLength = MyBMInfo.bmiHeader.biSizeImage;
		lpPixels = new BYTE[iconLength];

		MyBMInfo.bmiHeader.biCompression = BI_RGB;

		// Call GetDIBits a second time, this time to (format and) store the actual
		// bitmap data (the "pixels") in the buffer lpPixels		
		if ((res = GetDIBits(hdc, hSource, 0, MyBMInfo.bmiHeader.biHeight, (LPVOID)lpPixels, &MyBMInfo, DIB_RGB_COLORS)) == 0)
		{
			//Helper::BitmapInfoErrorExit(L"GetDIBits2()");
		}

		DeleteObject(hSource);
		ReleaseDC(NULL, hdcSource);

		/* iconLength è la dimensione dell'icona */
		/* Calcola lunghezza totale messaggio e salvala */
		msgLength = MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE +
			PROG_NAME_LENGTH + progNameLength + N_BYTE_TRATTINO + ICON_LENGTH_SIZE + iconLength;
		u_long netMsgLength = htonl(msgLength);

		memcpy(dimension, "--", 2 * N_BYTE_TRATTINO);
		memcpy(dimension + 2 * N_BYTE_TRATTINO, (void*)&netMsgLength, N_BYTE_MSG_LENGTH);
		memcpy(dimension + 2 * N_BYTE_TRATTINO + N_BYTE_MSG_LENGTH, "-", N_BYTE_TRATTINO);

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

		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH + progNameLength + N_BYTE_TRATTINO, &iconLength, N_BYTE_ICON_LENGTH);	// Aggiungi dimensione icona (4 byte)
		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH + progNameLength + N_BYTE_TRATTINO + N_BYTE_ICON_LENGTH, "-", N_BYTE_TRATTINO);	// Aggiungi trattino (1 byte)
		memcpy(finalBuffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH + progNameLength + N_BYTE_TRATTINO + ICON_LENGTH_SIZE, lpPixels, iconLength);	// Aggiungi dati icona
	}
	else if (op == CLOSE) {

		/* Calcola lunghezza totale messaggio e salvala */
		msgLength = MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH + progNameLength;
		u_long netMsgLength = htonl(msgLength);

		memcpy(dimension, "--", 2);
		memcpy(dimension + 2, (void*)&netMsgLength, 4);
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
	else if (op == FOCUS) {

		/* Calcola lunghezza totale messaggio e salvala */
		msgLength = MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH + progNameLength;
		u_long netMsgLength = htonl(msgLength);

		memcpy(dimension, "--", 2);
		memcpy(dimension + 2, (void*)&netMsgLength, 4);
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
		if (bytesSent < 0)
			break;
		remaining -= bytesSent;
		offset += bytesSent;
	}

	return;

}

int Server::receiveMessageFromClient(char* buffer, int bufferSize)
{
	int iResult = recv(clientSocket, buffer, bufferSize, 0);

	if (iResult > 0)
		return iResult;
	if (iResult == 0) {
		printMessage(TEXT("Chiusura connessione..."));
		printMessage(TEXT("\n"));
	}
	else if (iResult < 0) {
		int errorCode = WSAGetLastError();
		if (errorCode == WSAECONNRESET) {
			printMessage(TEXT("Connessione chiusa dal client."));
		}
		else
			printMessage(TEXT("recv() fallita con errore : " + WSAGetLastError()));		
	}

	/* Se si è arrivati qui, c'è stato un problema, quindi chiudi la connessione con il client. */
	//chiudiConnessioneClient();
}

void Server::printMessage(wstring string) {
	wcout << "[" << GetCurrentThreadId() << "] " << string << endl;
}

bool Server::validServer() 
{
	if (listeningSocket == INVALID_SOCKET)
		return false;

	return true;
}

bool Server::validClient()
{
	if (clientSocket == INVALID_SOCKET)
		return false;

	return true;
}

// TODO: da eliminare nella fase finale
SOCKET Server::getClientSocket()
{
	return clientSocket;
}

// TODO: da eliminare nella fase finale
SOCKET Server::getListeningSocket()
{
	return listeningSocket;
}

