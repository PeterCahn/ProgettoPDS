/* TODO:
	- Socket non bloccante?
	- Questione lista applicazioni ed app multithread: a Jure hanno detto che avrebbe dovuto mostrare i thread
	- Il reinterpret_cast è corretto? Cioè, è giusto usarlo dov'è usato?
*/

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <Winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <thread>
#include <chrono>

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
// #pragma comment (lib, "Mswsock.lib")

#define DEFAULT_BUFLEN 1024
#define DEFAULT_PORT  "27015"

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam);
SOCKET acceptConnection();
char* getForeground();
void receiveCommands(SOCKET* clientSocket);
void sendApplicationToClient(SOCKET* clientSocket, char* title);
DWORD WINAPI notificationsManagement(LPVOID lpParam);

int main(int argc, char* argv[])
{
	SOCKET clientSocket;
	
	while (true) {
		std::cout << "In attesa della connessione di un client..." << std::endl;
		clientSocket = acceptConnection();

		/* Stampa ed invia tutte le finestre */
		std::cout << "Applicazioni attive:" << std::endl;
		EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&clientSocket));		// Passa puntatore a socket come paramentro LPARAM opzionale
		send(clientSocket, "--END", strlen("--END"), 0);

		/* Crea thread che invia notifiche su cambiamento focus o lista programmi */
		DWORD notifThreadId;
		HANDLE notificationsThread = CreateThread(NULL, 0, notificationsManagement, &clientSocket, 0, &notifThreadId);
		if (notificationsThread == NULL)
			std::cout << "ERRORE nella creazione del thread 'notificationsThread'" << std::endl;

		/* Thread principale attende eventuali comandi */
		receiveCommands(&clientSocket);

		/* TODO: Termina thread notificationsThread 
		 * Attenzione! Chiamare TerminateThread o altro non è una buona pratica, 
		 * come specificato nelle slide, perchè non si fa il cleanup prima della
		 * morte del thread. Fare come scritto nelle slide
		 */

		/* Chiudi la connessione */
		int iResult = shutdown(clientSocket, SD_SEND);
		if (iResult == SOCKET_ERROR) {
			std::cout << "Chiusura della connessione fallita con errore: " << WSAGetLastError() << std::endl;
			closesocket(clientSocket);
			WSACleanup();
			return 1;
		}

		/* Cleanup */
		closesocket(clientSocket);
		WSACleanup();
	}
	return 0;
}

DWORD WINAPI notificationsManagement(LPVOID lpParam)
{
	// Reinterpreta LPARAM lParam come puntatore a SOCKET
	SOCKET* clientSocket = reinterpret_cast<SOCKET*>(lpParam);

	/* Stampa ed invia finestra col focus */
	char currentForeground[MAX_PATH];
	strcpy_s(currentForeground, MAX_PATH, getForeground());
	printf("Applicazione col focus:\n- %s\n", currentForeground);
	send(*clientSocket, currentForeground, strlen(currentForeground), 0);

	while (true) {
		// Esegui ogni mezzo secondo
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		char tempForeground[MAX_PATH];
		strcpy_s(tempForeground, MAX_PATH, getForeground());
		if (strcmp(tempForeground, currentForeground) != 0) {
			// Allora il programma che ha il focus è cambiato
			strcpy_s(currentForeground, MAX_PATH, tempForeground);
			std::cout << "Applicazione col focus cambiata! Ora e':" << std::endl << "- " << currentForeground << std::endl;
			char buf[MAX_PATH + 9];
			strcpy_s(buf, MAX_PATH + 9, "--FOCUS-");
			strcat_s(buf, MAX_PATH + 9, currentForeground);
			send(*clientSocket, buf, strlen(buf), 0);
		}
	}

}

char* getForeground() {
	char title[MAX_PATH];

	HWND handle = GetForegroundWindow();
	GetWindowText(handle, title, sizeof(title));
	if (strcmp(title, "") == 0)
		strcpy_s(title, MAX_PATH, "Desktop");

	return title;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	char class_name[80];
	char title[MAX_PATH];
	WINDOWINFO info;

	// Reinterpreta LPARAM lParam come puntatore a SOCKET 
	SOCKET* clientSocket = reinterpret_cast<SOCKET*>(lParam);

	if (IsWindowVisible(hwnd)) {
		GetWindowText(hwnd, title, sizeof(title));
		if (strlen(title) != 0) {
			std::cout << "- " << title << std::endl;
			sendApplicationToClient(clientSocket, title);
		}
	}

	return TRUE;
}

/* TODO: inviare anche l'icona */
void sendApplicationToClient(SOCKET* clientSocket, char* title) {
	send(*clientSocket, title, strlen(title), 0);
}

HICON getHICONfromHWND(HWND hwnd) {
	return (HICON)GetClassLong(hwnd, GCL_HICON);
}

HBITMAP convertHICONtoHBITMAP(HICON hIcon) {
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

SOCKET acceptConnection(void)
{
	WSADATA wsaData;
	int iResult;

	SOCKET listenSocket = INVALID_SOCKET;
	SOCKET clientSocket = INVALID_SOCKET;

	struct addrinfo *result = NULL;
	struct addrinfo addr;

	int iSendResult;
	char recvbuf[DEFAULT_BUFLEN];
	int recvbuflen = DEFAULT_BUFLEN;

	// Inizializza Winsock
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		std::cout << "WSAStartup() fallita con errore: " << iResult << std::endl;
		return 1;
	}

	ZeroMemory(&addr, sizeof(addr));
	addr.ai_family = AF_INET;
	addr.ai_socktype = SOCK_STREAM;
	addr.ai_protocol = IPPROTO_TCP;
	addr.ai_flags = AI_PASSIVE;

	// Resolve the server address and port
	iResult = getaddrinfo(NULL, DEFAULT_PORT, &addr, &result);
	if (iResult != 0) {
		std::cout << "getaddrinfo() fallita con errore: " << iResult << std::endl;
		WSACleanup();
		return 1;
	}

	// Create a SOCKET for connecting to server
	listenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (listenSocket == INVALID_SOCKET) {
		std::cout << "socket() fallita con errore: " << WSAGetLastError() << std::endl;
		freeaddrinfo(result);
		WSACleanup();
		return 1;
	}

	// Setup the TCP listening socket
	iResult = bind(listenSocket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		std::cout << "bind() fallita con errore: " << WSAGetLastError() << std::endl;
		freeaddrinfo(result);
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	freeaddrinfo(result);

	iResult = listen(listenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR) {
		std::cout << "listen() fallita con errore: " << WSAGetLastError() << std::endl;
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	// Accept a client socket
	clientSocket = accept(listenSocket, NULL, NULL);
	if (clientSocket == INVALID_SOCKET) {
		std::cout << "accept() fallita con errore: " << WSAGetLastError() << std::endl;
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	struct sockaddr clientSockAddr;
	int nameLength;
	getpeername(clientSocket, &clientSockAddr, &nameLength);
	struct sockaddr_in *s = (struct sockaddr_in *)&clientSockAddr;
	int port = ntohs(s->sin_port);
	char ipstr[INET_ADDRSTRLEN];
	/* TODO: Fix stampa indirizzo */
	inet_ntop(AF_INET, &(s->sin_addr), ipstr, INET_ADDRSTRLEN);
	std::cout << "Connessione stabilita con " << ipstr << ":" << port << std::endl;

	// No longer need server socket
	closesocket(listenSocket);

	return clientSocket;
}

void receiveCommands(SOCKET* clientSocket) {
	// Ricevi finchè il client non chiude la connessione
	char* recvbuf =(char*)malloc(sizeof(char)*DEFAULT_BUFLEN);
	int iResult;
	do {
		iResult = recv(*clientSocket, recvbuf, DEFAULT_BUFLEN, 0);
		if (iResult > 0) {
			/*
			TODO: a che serve quello che segue?

			printf("Bytes received: %d\n", iResult);

			// Echo the buffer back to the sender
			iSendResult = send(*clientSocket, recvbuf, iResult, 0);
			if (iSendResult == SOCKET_ERROR) {
				printf("send failed with error: %d\n", WSAGetLastError());
				closesocket(*clientSocket);
				WSACleanup();
				return;
			}
			printf("Bytes sent: %d\n", iSendResult);
			*/
		}
		else if (iResult == 0)
			std::cout << "Chiusura connessione...\n" << std::endl << std::endl;
		else {
			std::cout << "recv() fallita con errore: " << WSAGetLastError() << std::endl;;
			closesocket(*clientSocket);
			WSACleanup();
			return;
		}

	} while (iResult > 0);
	
}