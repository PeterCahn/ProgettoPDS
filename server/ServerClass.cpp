
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

#include "ServerClass.h"
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

ServerClass::ServerClass()
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


ServerClass::~ServerClass()
{
	// Terminates use of the Winsock 2 DLL (Ws2_32.dll)
	closesocket(listeningSocket);
	WSACleanup();
}

/* Acquisisce la porta verificando che sia un numero tra 1024 e 65535 */
void ServerClass::leggiPorta()
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

/* Avvia il server settando la listeningPort del Server */
void ServerClass::avviaServer()
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

		wcout << "[" << GetCurrentThreadId() << "] " << "Server in avvio..." << endl;
		
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

/* Attende una connessione in entrata da un client */
void ServerClass::acceptConnection(void)
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

void ServerClass::sendNotificationsToClient(operation op) {

}

bool ServerClass::validServer() 
{
	if (listeningSocket == INVALID_SOCKET)
		return false;

	return true;
}

bool ServerClass::validClient()
{
	if (clientSocket == INVALID_SOCKET)
		return false;

	return true;
}

// TODO: da eliminare nella fase finale
SOCKET ServerClass::getClientSocket()
{
	return clientSocket;
}

// TODO: da eliminare nella fase finale
SOCKET ServerClass::getListeningSocket()
{
	return listeningSocket;
}

