/* TODO:
	- Questione lista applicazioni ed app multithread: a Jure hanno detto che avrebbe dovuto mostrare i thread
	- Il reinterpret_cast è corretto? Cioè, è giusto usarlo dov'è usato?
	- Cos'è la finestra "Program Manager"?
	- Gestione finestra senza nome (Desktop)
	- Deallocazione risorse
	- Quando il client si chiude o si chiude anche il server (e non dovrebbe farlo) o se sopravvive alla prossima apertura di un client non funziona bene
	  perchè avvia un nuovo thread notificationsThread senza uccidere il precedente
	- Se due finestre hanno lo stesso nome ed una delle due è in focus, vengono entrambe segnate come in focus perchè non sa distinguere quale lo sia veramente, ma poi
	  solo la prima nella lista del client ha la percentuale che aumenta
	- Gestire caso in cui finestra cambia nome (es: "Telegram" con 1 notifica diventa "Telegram(1)")
	- std::promise globale va bene?
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

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam);
SOCKET acceptConnection();
string getForeground();
string getTitleFromHwnd(HWND hwnd);
void receiveCommands(SOCKET* clientSocket);
void sendApplicationToClient(SOCKET* clientSocket, HWND hwnd, operation op);
DWORD WINAPI notificationsManagement(LPVOID lpParam);
void sendKeystrokesToProgram(vector<UINT> vKeysList);
void ErrorExit(LPTSTR lpszFunction);
HICON getHICONfromHWND(HWND hwnd);
HBITMAP getHBITMAPfromHICON(HICON hIcon);
PBITMAPINFO CreateBitmapInfoStruct(HBITMAP hBmp);

promise<bool> stopNotificationThread;

int main(int argc, char* argv[])
{
	SOCKET clientSocket;

	while (true) {
		cout << "In attesa della connessione di un client..." << endl;
		clientSocket = acceptConnection();

		/* Crea thread che invia notifiche su cambiamento focus o lista programmi */
		stopNotificationThread = promise<bool>();	// Reimpostazione di promise prima di creare il thread in modo da averne una nuova, non già soddisfatta, ad ogni ciclo
		DWORD notifThreadId;		
		HANDLE notificationsThread = CreateThread(NULL, 0, notificationsManagement, &clientSocket, 0, &notifThreadId);
		if (notificationsThread == NULL)
			std::cout << "ERRORE nella creazione del thread 'notificationsThread'" << std::endl;

		/* Thread principale attende eventuali comandi */
		receiveCommands(&clientSocket);

		/* Procedura terminazione */
		stopNotificationThread.set_value(TRUE);
		//DWORD dwEvent = WaitForSingleObject(notificationsThread, INFINITE);	// Dopo avere segnalato al thread di terminare, aspetto che lo faccia
		CloseHandle(notificationsThread);	// Handle non più necessaria

		 /* Chiudi la connessione */
		int iResult = shutdown(clientSocket, SD_SEND);
		if (iResult == SOCKET_ERROR) {
			cout << "Chiusura della connessione fallita con errore: " << WSAGetLastError() << endl;
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
	cout << "Applicazioni attive:" << endl;
	vector<HWND> currentProgs;
	EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&currentProgs));
	cout << "Programmi aperti: " << endl;
	bool desktopAlreadySent = FALSE;
	for each (HWND hwnd in currentProgs) {
		string windowTitle = getTitleFromHwnd(hwnd);
		if (windowTitle.length() == 0 && !desktopAlreadySent) {
			desktopAlreadySent = TRUE;
			windowTitle = "Desktop";
		}
		else if (windowTitle.length() == 0 && desktopAlreadySent)
			continue;

		cout << "- " << windowTitle << endl;
		sendApplicationToClient(clientSocket, hwnd, OPEN);
	}

	/* Stampa ed invia finestra col focus */
	HWND currentForegroundHwnd = GetForegroundWindow();
	cout << "Applicazione col focus:" << endl << "- " << getTitleFromHwnd(currentForegroundHwnd) << endl;
	sendApplicationToClient(clientSocket, currentForegroundHwnd, FOCUS);

	/* Da qui in poi confronta quello che viene rilevato con quello che si ha */

	/* Controlla lo stato della variabile future: se è stata impostata dal thread principale, è il segnale che questo thread deve terminare */
	future<bool> f = stopNotificationThread.get_future();
	while (f.wait_for(std::chrono::seconds(0)) != future_status::ready) {
		// Esegui ogni mezzo secondo
		this_thread::sleep_for(chrono::milliseconds(500));

		/* Variazioni lista programmi */
		vector<HWND> tempProgs;
		EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&tempProgs));

		// Check nuova finestra
		for each(HWND currentHwnd in tempProgs) {
			if (find(currentProgs.begin(), currentProgs.end(), currentHwnd) == currentProgs.end()) {
				// currentProgs non contiene questo programma (quindi è stato aperto ora)
				string windowTitle = getTitleFromHwnd(currentHwnd);
				if (windowTitle.length() != 0) {
					currentProgs.push_back(currentHwnd);
					cout << "Nuova finestra aperta!" << endl << "- " << windowTitle << endl;
					sendApplicationToClient(clientSocket, currentHwnd, OPEN);
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
					cout << "Finestra chiusa!" << endl << "- " << windowTitle << endl;
					sendApplicationToClient(clientSocket, currentHwnd, CLOSE);
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
			cout << "Applicazione col focus cambiata! Ora e':" << endl << "- " << windowTitle << endl;
			sendApplicationToClient(clientSocket, currentForegroundHwnd, FOCUS);
		}
	}
	return 0;
}


string getTitleFromHwnd(HWND hwnd) {
	char title[MAX_PATH];

	GetWindowText(hwnd, title, sizeof(title));

	return string(title);
}

string getForeground() {
	HWND handle = GetForegroundWindow();
	return getTitleFromHwnd(handle);
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	// Reinterpreta LPARAM lParam come puntatore alla lista dei nomi 
	vector<HWND>* progNames = reinterpret_cast<vector<HWND>*>(lParam);

	// Aggiungi le handle dei programmi aperti al vector<HWND> ricevuto
	if (IsWindowVisible(hwnd)) 
		(*progNames).push_back(hwnd);
	

	return TRUE;
}

/* TODO: inviare anche l'icona (in progress) */
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
void sendApplicationToClient(SOCKET* clientSocket, HWND hwnd, operation op) {

	string progName(getTitleFromHwnd(hwnd));
	if (progName.length() == 0)
		progName = "Desktop";

	int lBuf = 0;

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
	strcat_s(buf, MAX_PATH + 12, to_string(progName.length()).c_str());
	strcat_s(buf, MAX_PATH + 12, "-");
	strcat_s(buf, MAX_PATH + 12, progName.c_str());

	lBuf = strlen(buf);
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
			ErrorExit("GetDIBits1()");
		}

		// create the pixel buffer
		lpPixels = new BYTE[MyBMInfo.bmiHeader.biSizeImage];

		MyBMInfo.bmiHeader.biCompression = BI_RGB;

		// Call GetDIBits a second time, this time to (format and) store the actual
		// bitmap data (the "pixels") in the buffer lpPixels		
		if ((res = GetDIBits(hdc, hSource, 0, MyBMInfo.bmiHeader.biHeight, (LPVOID)lpPixels, &MyBMInfo, DIB_RGB_COLORS)) == 0)
		{
			ErrorExit("GetDIBits2()");
		}

		int len = MyBMInfo.bmiHeader.biSizeImage;
		strcat_s(buf, MAX_PATH, "-");
		strcat_s(buf, MAX_PATH, to_string(len).c_str());
		strcat_s(buf, MAX_PATH, "-");

		/* Prepara un nuovo buffer con le ulteriori informazioni da inviare */
		BYTE *buffer = new BYTE[lBuf + to_string(len).length() + 2 + len];
		memcpy(buffer, buf, lBuf + to_string(len).length() + 2);
		memcpy(buffer + +lBuf + to_string(len).length() + 2, lpPixels, len);

		send(*clientSocket, (char*)buffer, lBuf + to_string(len).length() + 2 + len, 0);

		DeleteObject(hSource);
		ReleaseDC(NULL, hdcSource);
		delete[] lpPixels;
		delete[] buffer;
		return;
	}

	send(*clientSocket, buf, lBuf, 0);

	return;
}

HICON getHICONfromHWND(HWND hwnd) {

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

HBITMAP getHBITMAPfromHICON(HICON hIcon) {
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

PBITMAPINFO CreateBitmapInfoStruct(HBITMAP hBmp)
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
	cout << "Inserire la porta su cui ascoltare: ";
	string listeningPort;
	cin >> listeningPort;
	while (!cin.good()) {
		cout << "Errore nella lettura della porta da ascoltare, reinserirne il valore" << endl;
		cin >> listeningPort;
	}

	iResult = getaddrinfo(NULL, listeningPort.c_str(), &addr, &result);
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
	iResult = ::bind(listenSocket, result->ai_addr, (int)result->ai_addrlen);
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

	// Accetta la connessione
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
	inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof(ipstr));
	std::cout << "Connessione stabilita con " << ipstr << ":" << port << std::endl;

	// No longer need server socket
	closesocket(listenSocket);

	return clientSocket;
}

void receiveCommands(SOCKET* clientSocket) {
	// Ricevi finchè il client non chiude la connessione
	char* recvbuf = (char*)malloc(sizeof(char)*DEFAULT_BUFLEN);
	int iResult;
	do {
		iResult = recv(*clientSocket, recvbuf, DEFAULT_BUFLEN, 0);
		if (iResult == 0)
			std::cout << "Chiusura connessione...\n" << std::endl << std::endl;
		else if (iResult < 0) {
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

void ErrorExit(LPTSTR lpszFunction)
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
		(lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));
	StringCchPrintf((LPTSTR)lpDisplayBuf,
		LocalSize(lpDisplayBuf) / sizeof(TCHAR),
		TEXT("%s failed with error %d: %s"),
		lpszFunction, dw, lpMsgBuf);
	MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);

	LocalFree(lpMsgBuf);
	LocalFree(lpDisplayBuf);
	return;
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