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

#include "WindowsNotificationService.h"
#include "Helper.h"

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

WindowsNotificationService::WindowsNotificationService()
{
	_setmode(_fileno(stdout), _O_U16TEXT);

	/* Inizializza il server */
	server = Server();

	/* Inizializza l'exception_ptr per gestire eventuali exception nel background thread */
	globalExceptionPtr = nullptr;
	numberRetries = 0;
	retry = false;

	windows = map<HWND, wstring>();
	
	stopNotificationsThread = promise<bool>();	
}

WindowsNotificationService::~WindowsNotificationService()
{
	
}

void WindowsNotificationService::start()
{
	/* Tentativo di sganciare un thread per raccogliere i messaggi nella coda degli eventi windows delle finestre monitorate.
		Gestione eventi windows (semplificato ed adattato), preso spunto da qui: http://www.cplusplus.com/forum/windows/58791/
		NB: Togli commento dalla prossima riga per ascoltare gli eventi.
			Le righe successive non verranno eseguite perchè la hook esegue un ciclo while continuo (vedi funzione hook)
	*/	 
	//thread t(hook, this);

	/* Avvia il server */
	server.avviaServer();
	if (!server.validServer()) {
		printMessage(TEXT("Istanza di server creata non valida.Riprovare."));		
		return;
	}

	while (true) {

		/* Aspetta nuove connessioni in arrivo e si rimette in attesa se non è possibile accettare la connessione dal client */		
		while (true) {
			try {
				printMessage(TEXT("In attesa della connessione di un client..."));
				server.acceptConnection();
				if (server.validClient())
					break;
			}
			catch (exception& ex) {
				wcout << "[" << GetCurrentThreadId() << "] " << "Eccezione lanciata durante l'accettazione del client: " << ex.what() << endl;
			}
		}
		
		/* Crea thread che invia notifiche su cambiamento focus o lista programmi */
		stopNotificationsThread = promise<bool>();	// Reimpostazione di promise prima di creare il thread in modo da averne una nuova, non già soddisfatta, ad ogni ciclo
		try {
			/* Crea thread per gestire le notifiche */
			thread notificationsThread(&WindowsNotificationService::notificationsManagement, this);

			/* Thread principale attende eventuali comandi sulla finestra attualmente in focus */
			receiveCommands();	// ritorna quando la connessione con il client è chiusa

			/* Procedura terminazione thread notifiche: setta il value nella promise in modo che il notificationsmanagement esca */
			stopNotificationsThread.set_value(TRUE);
			notificationsThread.join();

			/* Se un'eccezione si è verificata nel background thread viene rilanciata nel main thread */
			if (globalExceptionPtr) rethrow_exception(globalExceptionPtr);
		}
		catch (system_error se) {			
			wcout << "[" << GetCurrentThreadId() << "] " << "ERRORE nella creazione del thread 'notificationsThread': " << se.what() << endl;
			//WSACleanup(); // Terminates use of the Winsock 2 DLL (Ws2_32.dll)
			continue;
		}
		catch (exception &ex)
		{
			wcout << "[" << GetCurrentThreadId() << "] " << "Thread 'notificationsThread' terminato con un'eccezione: " << ex.what() << endl;
			/* Riprova a lanciare il thread (?) */
			//wcout << "[" << GetCurrentThreadId() << "] " << "Tentativo riavvio 'notificationsThread' sul client" << endl;
			continue;				
		}

		/* Cleanup */
		server.chiudiConnessioneClient();
	}

	server.arrestaServer();
	//WSACleanup(); // Terminates use of the Winsock 2 DLL (Ws2_32.dll)
}

BOOL CALLBACK WindowsNotificationService::EnumWindowsProc(HWND hWnd, LPARAM lParam)
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

/* Usato per fare una cernita delle finestre restituite da EnumWindowsProc da aggiungere a 'windows' */
BOOL WindowsNotificationService::IsAltTabWindow(HWND hwnd)
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

void WINAPI WindowsNotificationService::notificationsManagement()
{
	try {

		/* Stampa ed invia tutte le finestre con flag OPEN */		
		EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windows));
		printMessage(TEXT("Finestre aperte:"));
		
		for each (pair<HWND, wstring> pair in windows) {
			wstring windowTitle = pair.second;
			printMessage(windowTitle);			
			server.sendNotificationToClient(pair.first, pair.second, OPEN);
		}

		/* Stampa ed invia finestra col focus con flag FOCUS */
		HWND currentForegroundHwnd = GetForegroundWindow();
		wstring title = Helper::getTitleFromHwnd(currentForegroundHwnd);
		printMessage(TEXT("Applicazione con il focus:"));
		printMessage(TEXT("-" + title));
		server.sendNotificationToClient(currentForegroundHwnd, title, FOCUS);

		/* Da qui in poi confronta quello che viene rilevato con quello che si ha */
		
		/* Controlla lo stato della variabile future: se è stata impostata dal thread principale, è il segnale che questo thread deve terminare */
		future<bool> f = stopNotificationsThread.get_future();
		while (f.wait_for(chrono::seconds(0)) != future_status::ready) {
			// Esegui ogni mezzo secondo
			this_thread::sleep_for(chrono::milliseconds(500));

			/* Variazioni lista programmi */
			map<HWND, wstring> tempWindows;
			::EnumWindows(&WindowsNotificationService::EnumWindowsProc, reinterpret_cast<LPARAM>(&tempWindows));

			// Check nuova finestra
			for each (pair<HWND, wstring> pair in tempWindows) {
				if (windows.find(pair.first) == windows.end()) {
					// 'windows' non contiene questo programma (quindi è stato aperto ora)
					wstring windowTitle = pair.second;
					
					// Devo aggiungere la finestra a 'windows'
					windows[pair.first] = windowTitle;
					printMessage(TEXT("Nuova finestra aperta!"));
					printMessage(TEXT("- " + windowTitle));
					server.sendNotificationToClient(pair.first, pair.second, OPEN);					
				}
			}

			// Check chiusura finestra
			vector<HWND> toBeDeleted;
			for each (pair<HWND, wstring> pair in windows) {
				if (tempWindows.find(pair.first) == tempWindows.end()) {
					// tempWindows non contiene più pair.first (quindi è stata chiusa)
					wstring windowTitle = pair.second;

					printMessage(TEXT("Finestra chiusa!"));
					printMessage(TEXT("- " + windowTitle));
					server.sendNotificationToClient(pair.first, pair.second, CLOSE);
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
						printMessage(TEXT("Cambio nome per la finestra: "));
						printMessage(TEXT("\t " + previousTitle));
						printMessage(TEXT("La finestra ora in focus è: "));
						printMessage(TEXT("- " + newTitle));
						server.sendNotificationToClient(pair.first, pair.second, TITLE_CHANGED);
					}
				}
			}

			/* Check variazione focus */
			HWND tempForeground = GetForegroundWindow();
			if (tempForeground != currentForegroundHwnd) {
				// Allora il programma che ha il focus è cambiato				
				currentForegroundHwnd = tempForeground;

				wstring windowTitle = Helper::getTitleFromHwnd(currentForegroundHwnd);

				printMessage(TEXT("Applicazione col focus cambiata! Ora e':"));
				printMessage(TEXT("- " + windowTitle));
				server.sendNotificationToClient(currentForegroundHwnd, windowTitle, FOCUS);				
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

		send(server.getClientSocket(), sendBuf, 12, 0);

	}
}

void WindowsNotificationService::receiveCommands() {

	// Ricevi finchè il client non chiude la connessione
	char recvbuf[DEFAULT_BUFLEN*sizeof(char)];
	char sendBuf[DEFAULT_BUFLEN*sizeof(char)];

	int iResult;
	do {
		iResult = server.receiveMessageFromClient(recvbuf, DEFAULT_BUFLEN);
		if (iResult == 0) {
			printMessage(TEXT("Chiusura connessione..."));
			printMessage(TEXT("\n"));
		}
		else if (iResult < 0) {
			int errorCode = WSAGetLastError();
			if (errorCode == WSAECONNRESET) {
				printMessage(TEXT("Connessione chiusa dal client."));
			}else
				printMessage(TEXT("recv() fallita con errore : " + WSAGetLastError()));

			server.chiudiConnessioneClient();
			return;
		}
		/* Se ricevo "--CLOSE-" il client vuole disconnettersi: invio la conferma ed esco */
		else if (strncmp(recvbuf, "--CLSCN-", 8) == 0) {			

			u_long msgLength = 5;
			u_long netMsgLength = htonl(msgLength);

			memcpy(sendBuf, "--", 2);
			memcpy(sendBuf + 2, (void*)&netMsgLength, 4);
			memcpy(sendBuf + 6, "-", 1);

			memcpy(sendBuf + 7, "OKCLO-", 5);
			
			send(server.getClientSocket(), sendBuf, 12, 0);
			printMessage(TEXT("Connessione con il client chiusa.\n"));
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

void WindowsNotificationService::sendKeystrokesToProgram(std::vector<UINT> vKeysList)
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
}

/* La funzione MapVirtualKey() traduce virtualKeys in char o "scan codes" in Virtual-keys
* Settandone il primo parametro a MAPVK_VSC_TO_VK_EX, tradurrà il secondo paramentro, che dovrà
* essere uno "scan code", in una Virtual-key.
* Se usassimo KEYEVENTF_UNICODE in dwFlags, dovrebbe essere settato a 0
*/

void WindowsNotificationService::printMessage(wstring string) {
	wcout << "[" << GetCurrentThreadId() << "] " << string << endl;
}

/* Questa funzione viene passata alla SetWinEventHook nella funzione hook per gestire gli eventi */
void CALLBACK WindowsNotificationService::HandleWinEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
	TCHAR title[MAX_PATH];
	GetWindowTextW(hwnd, title, sizeof(title));

	wstring t = wstring(title);

	map<HWND, wstring> tempWindows;
	EnumWindows(&WindowsNotificationService::EnumWindowsProc, reinterpret_cast<LPARAM>(&tempWindows));

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

		if (event == EVENT_OBJECT_FOCUS || event == EVENT_SYSTEM_FOREGROUND)
			wcout << "New focus: [" << hwnd << "] " << t << endl;
		else if (event == EVENT_OBJECT_NAMECHANGE)
			wcout << "Name changed: [" << hwnd << "] " << t << endl;
		else if (event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_UNCLOAKED || event == EVENT_OBJECT_SHOW)
			wcout << "Finestra aperta: [" << hwnd << "] " << t << endl;
		else if (event == EVENT_OBJECT_CLOAKED || event == EVENT_OBJECT_DESTROY || event == EVENT_OBJECT_STATECHANGE)
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

unsigned __stdcall WindowsNotificationService::hook(void* args)
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
		EnumWindows(&WindowsNotificationService::EnumWindowsProc, reinterpret_cast<LPARAM>(&tempWindows));

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

