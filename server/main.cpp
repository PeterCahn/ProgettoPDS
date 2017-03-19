/* TODO:
	- Questione lista applicazioni ed app multithread: a Jure hanno detto che avrebbe dovuto mostrare i thread
	- Il reinterpret_cast è corretto? Cioè, è giusto usarlo dov'è usato?
	- Cos'è la finestra "Program Manager"?
	- Gestione finestra senza nome (Desktop)
	- Deallocazione risorse
	- CRASH quando il client genera un'eccezione !!!!
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
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
// #pragma comment (lib, "Mswsock.lib")

#define DEFAULT_BUFLEN 1024
#define DEFAULT_PORT  "27015"

/* Definisce che tipo di notifica è associata alla stringa rappresentante il nome di un finestra da inviare al client */
enum operation {
	OPEN,
	CLOSE,
	FOCUS
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam);
SOCKET acceptConnection();
char* getForeground();
void receiveCommands(SOCKET* clientSocket); 
void sendApplicationToClient(SOCKET* clientSocket, std::string progName, operation op);
DWORD WINAPI notificationsManagement(LPVOID lpParam);
void sendKeystrokesToProgram(std::vector<UINT> vKeysList);

int main(int argc, char* argv[])
{
	SOCKET clientSocket;
	
	while (true) {
		std::cout << "In attesa della connessione di un client..." << std::endl;
		clientSocket = acceptConnection();

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

	/* Stampa ed invia tutte le finestre */
	std::cout << "Applicazioni attive:" << std::endl;
	std::vector<std::string> currentProgNames;
	// Aggiungi la finstra di default (Desktop)
	currentProgNames.push_back("Desktop");
	EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&currentProgNames));
	std::cout << "Programmi aperti: " << std::endl;
	for each (std::string progName in currentProgNames) {
		std::cout << "- " << progName << std::endl;
		sendApplicationToClient(clientSocket, progName, OPEN);
	}

	/* Stampa ed invia finestra col focus */
	char currentForeground[MAX_PATH];
	strcpy_s(currentForeground, MAX_PATH, getForeground());
	printf("Applicazione col focus:\n- %s\n", currentForeground);
	sendApplicationToClient(clientSocket, currentForeground, FOCUS);

	/* Da qui in poi confronta quello che viene rilevato con quello che si ha */
	while (true) {
		// Esegui ogni mezzo secondo
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

		/* Variazioni lista programmi */
		std::vector<std::string> tempProgNames;
		EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&tempProgNames));
		// Check nuova finestra
		for each(std::string tempProgName in tempProgNames) {
			if (std::find(currentProgNames.begin(), currentProgNames.end(), tempProgName) == currentProgNames.end()) {
				// currentProgNames non contiene questo programma (quindi è stato aperto ora)
				currentProgNames.push_back(tempProgName);
				std::cout << "Nuova finestra aperta!" << std::endl << "- " << tempProgName << std::endl;
				sendApplicationToClient(clientSocket, tempProgName, OPEN);
			}
		}
		
		// Check chiusura finestra
		std::vector<std::string> toBeDeleted;
		for each (std::string currentProgName in currentProgNames) {
			if ((currentProgName.compare("Desktop") != 0) && 
				(std::find(tempProgNames.begin(), tempProgNames.end(), currentProgName) == tempProgNames.end())) {
				// tempProgNames non contiene più currentProgName
				std::cout << "Finestra chiusa!" << std::endl << "- " << currentProgName << std::endl;
				sendApplicationToClient(clientSocket, currentProgName, CLOSE);
				toBeDeleted.push_back(currentProgName);
			}
		}
		for each (std::string deleteThis in toBeDeleted) {
			// Ricava index di deleteThis in currentProgNames per cancellarlo
			auto index = std::find(currentProgNames.begin(), currentProgNames.end(), deleteThis);
			currentProgNames.erase(index);
		}		
		
		/* Variazioni focus */
		char tempForeground[MAX_PATH];
		strcpy_s(tempForeground, MAX_PATH, getForeground());
		if (strcmp(tempForeground, currentForeground) != 0) {
			// Allora il programma che ha il focus è cambiato
			strcpy_s(currentForeground, MAX_PATH, tempForeground);
			if (strcmp(currentForeground, "") == 0)
				strcpy_s(currentForeground, MAX_PATH, "Desktop");
			std::cout << "Applicazione col focus cambiata! Ora e':" << std::endl << "- " << currentForeground << std::endl;
			sendApplicationToClient(clientSocket, currentForeground, FOCUS);
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
	std::vector<std::string>* progNames = (std::vector<std::string>*)(lParam);

	// Aggiungi i nomi dei programmi aperti al vector<std::string> ricevuto
	if (IsWindowVisible(hwnd)) {
		GetWindowText(hwnd, title, sizeof(title));
		if (strlen(title) != 0) 
			(*progNames).push_back(title);
	}

	return TRUE;
}

/* TODO: inviare anche l'icona */
/* Invia il nome della finestra e l'informazione ad esso associata al client 
 * Il formato del messaggio è --<operazione>-<lunghezza_nome_finestra>-<nomefinestra>
 */
void sendApplicationToClient(SOCKET* clientSocket, std::string progName, operation op) {
	
	char buf[MAX_PATH + 12];	// 12 sono i caratteri aggiuntivi alla lunghezza massima del nome di una finestra, dati dal formato del messaggio
	if (op == OPEN) {
		strcpy_s(buf, MAX_PATH + 12, "--OPENP-");
	}
	else if (op == CLOSE) {
		strcpy_s(buf, MAX_PATH + 12, "--CLOSE-");
	}
	else {
		strcpy_s(buf, MAX_PATH + 12, "--FOCUS-");
	}	
	strcat_s(buf, MAX_PATH + 12, std::to_string(progName.length()).c_str());
	strcat_s(buf, MAX_PATH + 12, "-");
	strcat_s(buf, MAX_PATH + 12, progName.c_str());
	
	send(*clientSocket, buf, strlen(buf), 0);
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
		if (iResult == 0)
			std::cout << "Chiusura connessione...\n" << std::endl << std::endl;
		else if(iResult < 0) {
			std::cout << "recv() fallita con errore: " << WSAGetLastError() << std::endl;;
			closesocket(*clientSocket);
			WSACleanup();
			return;
		}

		// Questa stringa contiene i virtual-keys ricevuti separati da '+'
		std::string stringaRicevuta(recvbuf);
		
		// Converti la stringa in una lista di virtual-keyes
		std::stringstream sstream(stringaRicevuta);
		std::string virtualKey;
		std::vector<UINT> vKeysList;
		while (std::getline(sstream, virtualKey, '+'))	// ogni virtual-key è seprata dalle altre dal carattere '+'
		{
			UINT vKey;
			sscanf_s(virtualKey.c_str(), "%u", &vKey);
			vKeysList.push_back(vKey);
		}

		// TODO: reimuovere dopo debug (?)
		// Stampa codici virtual-key ricevute
		std::cout << "Virtual-key ricevute da inviare alla finestra in focus:" << std::endl;
		for each(UINT i in vKeysList)
			std::cout << "- " << i << std::endl;

		// Invia keystrokes all'applicazione in focus
		sendKeystrokesToProgram(vKeysList);

	} while (iResult > 0);
	
}

void sendKeystrokesToProgram(std::vector<UINT> vKeysList)
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
	for (i = 0; i < keystrokes_lenght; ++i)	{
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

	std::cout << "Keystrokes to send: " << keystrokes_lenght << std::endl;
	std::cout << "Keystrokes sent: " << keystrokes_sent << std::endl;
}

/* La funzione MapVirtualKey() traduce virtualKeys in char o "scan codes" in Virtual-keys
 * Settandone il primo parametro a MAPVK_VSC_TO_VK_EX, tradurrà il secondo paramentro, che dovrà
 * essere uno "scan code", in una Virtual-key.
 * Se usassimo KEYEVENTF_UNICODE in dwFlags, dovrebbe essere settato a 0
 */