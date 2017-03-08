/* TODO:
	- Socket non bloccante ?
	- Questione app multithread: a Jure hanno detto che avrebbe dovuto mostrare i thread
*/

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <Winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
// #pragma comment (lib, "Mswsock.lib")

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT  "27015"

using namespace std;

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam);
SOCKET acceptConnection();
void getForeground(SOCKET *clientSocket);
void sendApplicationToClient(SOCKET* clientSocket, char* title);

int main(int argc, char* argv[])
{
	SOCKET clientSocket;
	
	cout << "In attesa della connessione di un client..." << endl;
	clientSocket = acceptConnection();

	/* Stampa ed invia tutte le finestre */
	cout << "Applicazioni attive:" << endl;
	EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&clientSocket));		// Passa puntatore a socket come paramentro LPARAM opzionale
	send(clientSocket, "--END", strlen("--END"), 0);

	/* Stampa ed invia finestra col focus */
	getForeground(&clientSocket);

	Sleep(5000000);
}

void getForeground(SOCKET *clientSocket) {
	char title[MAX_PATH];

	HWND handle = GetForegroundWindow();
	GetWindowText(handle, title, sizeof(title));

	cout << "Applicazione col focus:" << endl << "- " << title << endl;
	send(*clientSocket, title, strlen(title), 0);

}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	char class_name[80];
	char title[MAX_PATH];
	WINDOWINFO info;

	/* Reinterpreta LPARAM come puntatore a SOCKET */
	SOCKET* clientSocket = reinterpret_cast<SOCKET*>(lParam);

	if (IsWindowVisible(hwnd)) {
		GetWindowText(hwnd, title, sizeof(title));
		if (strlen(title) != 0) {
			cout << "- " << title << endl;
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

	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		printf("WSAStartup failed with error: %d\n", iResult);
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
		printf("getaddrinfo failed with error: %d\n", iResult);
		WSACleanup();
		return 1;
	}

	// Create a SOCKET for connecting to server
	listenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (listenSocket == INVALID_SOCKET) {
		printf("socket failed with error: %ld\n", WSAGetLastError());
		freeaddrinfo(result);
		WSACleanup();
		return 1;
	}

	// Setup the TCP listening socket
	iResult = bind(listenSocket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("bind failed with error: %d\n", WSAGetLastError());
		freeaddrinfo(result);
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	freeaddrinfo(result);

	iResult = listen(listenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR) {
		printf("listen failed with error: %d\n", WSAGetLastError());
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	// Accept a client socket
	clientSocket = accept(listenSocket, NULL, NULL);
	if (clientSocket == INVALID_SOCKET) {
		printf("accept failed with error: %d\n", WSAGetLastError());
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
	cout << "Connessione stabilita con " << ipstr << ":" << port;

	// No longer need server socket
	closesocket(listenSocket);

	return clientSocket;
}

/* 
CODICE PRECEDENTEMENTe IN SOCKET() DA RIPOSIZIONARE


// Receive until the peer shuts down the connection
do {
	iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
	if (iResult > 0) {
		printf("Bytes received: %d\n", iResult);

		// Echo the buffer back to the sender
		iSendResult = send(ClientSocket, recvbuf, iResult, 0);
		if (iSendResult == SOCKET_ERROR) {
			printf("send failed with error: %d\n", WSAGetLastError());
			closesocket(ClientSocket);
			WSACleanup();
			return 1;
		}
		printf("Bytes sent: %d\n", iSendResult);
	}
	else if (iResult == 0)
		printf("Connection closing...\n");
	else {
		printf("recv failed with error: %d\n", WSAGetLastError());
		closesocket(ClientSocket);
		WSACleanup();
		return 1;
	}

} while (iResult > 0);

// shutdown the connection since we're done
iResult = shutdown(ClientSocket, SD_SEND);
if (iResult == SOCKET_ERROR) {
	printf("shutdown failed with error: %d\n", WSAGetLastError());
	closesocket(ClientSocket);
	WSACleanup();
	return 1;
}

// cleanup
closesocket(ClientSocket);
WSACleanup();

return 0;
*/