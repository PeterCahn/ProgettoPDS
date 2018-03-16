/* TODO:
- Deallocazione risorse
- Verificare se il thread muore davvero in ogni situazione critica
- Gestione eccezioni
- Si riesce a terminare l'invio di finestre al client corrente e ad aspettare il prossimo,
	ma non a terminare il server mentre è in attesa sulla accept o sulla lettura della porta.
	Questo avviene SOLO quando si è già provato a chiudere una connessione.
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
#include "Message.h" // qui sono definiti i tipi di operazione

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
// #pragma comment (lib, "Mswsock.lib")

#define DEFAULT_BUFLEN 1024

#define MAX_RETRIES 3

using namespace std;


WindowsNotificationService::WindowsNotificationService()
{
	_setmode(_fileno(stdout), _O_U16TEXT);

	/* Inizializza l'exception_ptr per gestire eventuali exception nel background thread */
	globalExceptionPtr = nullptr;
	numberRetries = 0;
	retry = false;

	windows = map<HWND, wstring>();

}

WindowsNotificationService::~WindowsNotificationService()
{
	printMessage(TEXT("WindowsNotificationsService chiuso."));

}

/* Per uscire dal servizio */
volatile bool isRunning = true;
BOOL WINAPI HandlerRoutine(_In_ DWORD dwCtrlType) {
	switch (dwCtrlType)
	{
	case CTRL_C_EVENT:
		//printf("[Ctrl]+C\n");
		isRunning = false;
		// Signal is handled - don't pass it on to the next handler
		return TRUE;
	default:
		// Pass signal on to the next handler
		return FALSE;
	}
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
	if (server.avviaServer() < 0)
		throw exception("Impossibile avviare il server.");
	if (!server.validServer()) {
		printMessage(TEXT("Istanza di server creata non valida.Riprovare."));
		return;
	}

	/* Settato a false dall'handler per CTRL-C (TODO: così è visto solo alla prossima iterazione) */
	while (isRunning) {

		/* Aspetta nuove connessioni in arrivo e si rimette in attesa se non è possibile accettare la connessione dal client */
		if (server.acceptConnection() < 0)
			throw exception("Impossibile accettare nuove connessioni.");

		/* Setta la control routine per gestire il CTRL-C: chiude la connessione con il client per rimettersi in attesa */
		if (!SetConsoleCtrlHandler(HandlerRoutine, TRUE)) {
			printMessage(TEXT("ERRORE: Impossibile settare il control handler."));
			return;
		}

		/* Crea thread che invia notifiche su cambiamento focus o lista programmi */
		stopNotificationsThread = promise<bool>();	// Reimpostazione di promise prima di creare il thread in modo da averne una nuova, non già soddisfatta, ad ogni ciclo
		try {
			/* Crea thread per gestire le notifiche */
			thread notificationsThread(&WindowsNotificationService::notificationsManagement, this);

			/* Thread principale attende eventuali comandi sulla finestra attualmente in focus e messaggi dal client */
			receiveCommands();	// ritorna quando la connessione con il client è chiusa

			/* Procedura terminazione thread notifiche: setta il value nella promise in modo che il notificationsManagement esca */
			stopNotificationsThread.set_value(TRUE);
			notificationsThread.join();

			/* Se un'eccezione si è verificata ed è stata settata nel background thread, viene rilanciata ora nel main thread */
			if (globalExceptionPtr) rethrow_exception(globalExceptionPtr);

		}
		catch (system_error se) {
			wcout << "[" << GetCurrentThreadId() << "] " << "ERRORE nella creazione del thread 'notificationsThread': " << se.what() << endl;
			return;
		}
		catch (exception &ex)
		{
			// Si è verificata un'eccezione nei thread che gestiscono la connessione con il client.		
			/* Setta la control routine per gestire il CTRL-C */
			if (!SetConsoleCtrlHandler(HandlerRoutine, FALSE)) {
				printMessage(TEXT("ERRORE: Impossibile settare il control handler."));
				return;
			}
			server.chiudiConnessioneClient();
			continue;
		}

		/* Chiudi connessione con il client prima di provare a reiterare sul while */
		server.chiudiConnessioneClient();

		/* Se è stato premuto CTRL-C 'isRunning' è a false e quindi si può evitare di ciclare di nuovo uscendo dal servizio */
		if (isRunning) {
			/* Setta la control routine per gestire il CTRL-C */
			if (!SetConsoleCtrlHandler(HandlerRoutine, FALSE)) {
				printMessage(TEXT("ERRORE: Impossibile settare il control handler."));
				return;
			}
			continue;
		}
	}
}

void WindowsNotificationService::stop()
{
	server.arrestaServer();
}

BOOL CALLBACK WindowsNotificationService::EnumWindowsProc(HWND hWnd, LPARAM lParam)
{
	map<HWND, wstring>* windows2 = reinterpret_cast<map<HWND, wstring>*> (lParam);

	//DWORD process, thread;
	//thread = GetWindowThreadProcessId(hWnd, &process);
	
	wstring windowTitle = Helper::getTitleFromHwnd(hWnd);

	// Proteggere accesso a variabile condivisa "windows"
	if (IsAltTabWindow(hWnd))
		windows2->insert(pair<HWND, wstring>(hWnd, windowTitle));

	return TRUE;
}

/* Usato per fare una cernita delle finestre restituite da EnumWindowsProc da aggiungere a 'windows' */
BOOL WindowsNotificationService::IsAltTabWindow(HWND hwnd)
{
	TITLEBARINFO ti;
	HWND hwndTry, hwndWalk = NULL;

	if (!IsWindowVisible(hwnd))
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
	while (hwndTry != hwndWalk)
	{
		hwndWalk = hwndTry;
		hwndTry = GetLastActivePopup(hwndWalk);
		if (IsWindowVisible(hwndTry))
			break;
	}
	if (hwndWalk != hwnd)
		return FALSE;

	// the following removes some task tray programs and "Program Manager"
	ti.cbSize = sizeof(ti);
	GetTitleBarInfo(hwnd, &ti);
	if (ti.rgstate[0] & STATE_SYSTEM_INVISIBLE)
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
		while (f.wait_for(chrono::seconds(0)) != future_status::ready && isRunning) {
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
						server.sendNotificationToClient(pair.first, newTitle, TITLE_CHANGED);
					}
				}
			}

			/* Check variazione focus */
			HWND tempForeground = GetForegroundWindow();
			if (!IsAltTabWindow(tempForeground)) 
				tempForeground = 0;	// HWND settato a 0 se tempForeground non è una window di interesse

			if (tempForeground != currentForegroundHwnd) {
				// Allora il programma che ha il focus è cambiato.
				// Non c'è bisogno di vedere se questa tempForeground è una nuova finestra perché verrà rilevata al prossimo ciclo.

				wstring windowTitle = Helper::getTitleFromHwnd(tempForeground);

				if (tempForeground != 0 && tempWindows.find(tempForeground) == tempWindows.end()) {
					// E' una finestra che non è stata ancora inviata, quindi invia notifica OPEN
					server.sendNotificationToClient(tempForeground, windowTitle, OPEN);
				}

				// E' una finestra che è gia stata inviata, quindi notifica il cambio focus
				printMessage(TEXT("Applicazione col focus cambiata! Ora e':"));
				printMessage(TEXT("- " + windowTitle));
								
				server.sendNotificationToClient(tempForeground, windowTitle, FOCUS);				
				currentForegroundHwnd = tempForeground;
			}

			swap(windows, tempWindows);

			/* Check se è stato premuto CTRL-C (e isRunning è diventato false): in caso positivo,
				manda un messaggio al client per chiudere la connessione*/
			if (!isRunning) {
				printMessage(TEXT("Gestione finestre in chiusura..."));
				server.sendMessageToClient(ERROR_CLOSE);
				isRunning = true;
				return;
				//throw exception("Chiusura forzata.");
			}
		}

	}
	catch (future_error)
	{
		// cosa fare?
		globalExceptionPtr = current_exception();
		return;
	}
	catch (exception)
	{
		// catch anything thrown within try block that derives from std::exception			
		globalExceptionPtr = current_exception();
		return;
	}
	catch (...)
	{
		//Set the global exception pointer in case of an exception
		globalExceptionPtr = current_exception();

		/* E' stata scatenata un'eccezione. Notificalo al client per chiudere la connessione. */
		server.sendMessageToClient(ERROR_CLOSE);
	}
}

using json = nlohmann::json;
void WindowsNotificationService::receiveCommands() {

	// Ricevi finchè il client non chiude la connessione
	char recvbuf[DEFAULT_BUFLEN * sizeof(char)];
	char sendBuf[DEFAULT_BUFLEN * sizeof(char)];

	int iResult;
	do {
		iResult = server.receiveMessageFromClient(recvbuf, DEFAULT_BUFLEN);
		if (iResult <= 0) {	// c'è stato qualche errore nella connessione con il client
			return;
		}
		else {
			// Ottieni la stringa ricevuta dal client
			string stringaRicevuta(recvbuf);
			json j;

			try {
				j = json::parse(stringaRicevuta);
			}
			catch (json::exception e) {
				printMessage(TEXT("Json ricevuto dal client malformato."));
				continue;
			}
			if (j.find("operation") != j.end()) {
				// C'è il campo "operation"
				if (j["operation"] == "CLSCN") {

					printMessage(TEXT("Il client ha richiesta la disconnessione."));
				}
				else if (j["operation"] == "comando") {
					string virtualKey, stringUpToPlus;
					vector<INPUT> keystroke;
					
					int tempHwnd = (int)j["hwnd"];
					HWND targetHwnd = (HWND)tempHwnd;

					string keystrokeString = j["tasti"];
					string virtualKeyString;
					for (int i = 0; i < keystrokeString.size(); i++) {
						UINT vKey;
						if (keystrokeString[i] == '+') {
							// Aggiungi keyDown
							sscanf_s(virtualKeyString.c_str(), "%u", &vKey);
							INPUT input;
							input.type = INPUT_KEYBOARD;			// Definisce il tipo di input, che può essere INPUT_HARDWARE, INPUT_KEYBOARD o INPUT_MOUSE
																	// Una volta definito il tipo di input come INPUT_KEYBOARD, si usa la sotto-struttura .ki per inserire le informazioni sull'input
							input.ki.wVk = 0;						// Virtual-key code dell'input.	
							input.ki.wScan = MapVirtualKey(vKey, MAPVK_VK_TO_VSC);	// Se usassimo KEYEVENTF_UNICODE in dwFlags, wScan specificherebbe il carettere UNICODE da inviare alla finestra in focus
							if (isExtendedKey(vKey))				// Eventuali informazioni addizionali sull'evento (specifica se si tratta di una extendedKey o no)
								input.ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_SCANCODE;
							else	
								input.ki.dwFlags = KEYEVENTF_SCANCODE;
							input.ki.time = 0;						// Timestamp dell'evento. Settandolo a 0, il SO lo imposta in automatico
							input.ki.dwExtraInfo = 0;				// Valore addizionale associato al keystroke, servirebbe ad indicare che il tasto premuto fa parte del tastierino numerico
							keystroke.push_back(input);

							virtualKeyString.clear();
						}

						else if (keystrokeString[i] == '-') {
							// Aggiungi keyUp
							sscanf_s(virtualKeyString.c_str(), "%u", &vKey);
							INPUT input;
							input.type = INPUT_KEYBOARD;			// Definisce il tipo di input, che può essere INPUT_HARDWARE, INPUT_KEYBOARD o INPUT_MOUSE
																	// Una volta definito il tipo di input come INPUT_KEYBOARD, si usa la sotto-struttura .ki per inserire le informazioni sull'input
							input.ki.wVk = 0;						// Virtual-key code dell'input.	
							input.ki.wScan = MapVirtualKey(vKey, MAPVK_VK_TO_VSC);	// Se usassimo KEYEVENTF_UNICODE in dwFlags, wScan specificherebbe il carettere UNICODE da inviare alla finestra in focus
							if (isExtendedKey(vKey))				// Eventuali informazioni addizionali sull'evento (qui anche il fatto che sia un keyUp e non keyDown)
								input.ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP | KEYEVENTF_SCANCODE;
							else
								input.ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_SCANCODE;
							input.ki.time = 0;						// Timestamp dell'evento. Settandolo a 0, il SO lo imposta in automatico
							input.ki.dwExtraInfo = 0;				// Valore addizionale associato al keystroke, servirebbe ad indicare che il tasto premuto fa parte del tastierino numerico
							keystroke.push_back(input);

							virtualKeyString.clear();
						}
						else {
							virtualKeyString += keystrokeString[i];
						}
					}

					// TODO: rimuovere dopo debug
					// Stampa codici virtual-key ricevute
					wcout << "Virtual-key ricevute da inviare alla finestra in focus: " << endl; // << stringaRicevuta << std::endl;
					for each(INPUT key in keystroke)
						wcout << "- " << key.ki.wVk << " - " << key.ki.dwFlags << std::endl;

					// Invia keystroke all'applicazione in focus
					sendKeystrokesToProgram(targetHwnd, keystroke);

					ZeroMemory(recvbuf, sizeof(recvbuf));
				}
			}
		}

	} while (iResult > 0 && isRunning);

}

bool WindowsNotificationService::isExtendedKey(WORD virtualKey) {
	return virtualKey == VK_RMENU		// ALT TODO aggiungere altri
		|| virtualKey == VK_RCONTROL	// CTRL
		|| virtualKey == VK_INSERT		// INS
		|| virtualKey == VK_DELETE		// DEL
		|| virtualKey == VK_HOME		// HOME
		|| virtualKey == VK_END			// END
		|| virtualKey == VK_LEFT		// Arrow LEFT
		|| virtualKey == VK_UP			// Arrow UP
		|| virtualKey == VK_RIGHT		// Arrow RIGHT
		|| virtualKey == VK_DOWN		// Arrow DOWN
		|| virtualKey == VK_PRIOR		// Page UP
		|| virtualKey == VK_NEXT		// Page DOWN
		|| virtualKey == VK_NUMLOCK		// Num LOCK
		|| virtualKey == VK_SNAPSHOT	// PRINT SCREEN (screenshot)
		|| virtualKey == VK_DIVIDE		// DIVIDE
		|| virtualKey == VK_CANCEL;		// CANCEL / BREAK (ctrl+end)
}

void WindowsNotificationService::sendKeystrokesToProgram(HWND targetHwnd, std::vector<INPUT> vKeysList)
{
	int i, keystroke_sent;
	HWND progHandle;

	// Controlla che il keystroke sia ben formato
	vector<WORD> checkList;
	for each (INPUT input in vKeysList) {
		if (input.ki.dwFlags == KEYEVENTF_KEYUP) {
			// Controlla che il tasto sia stato precedentemente premuto
			vector<WORD>::iterator pos = find(checkList.begin(), checkList.end(), input.ki.wVk);
			if (pos == checkList.end()) {
				wcout << "ERRORE! Il comando ricevuto non è ben formato." << endl;
				return;
			}
			// Se cè, rimuovilo dalla lista
			checkList.erase(pos);

		}
		else if (input.ki.dwFlags == 0)
			checkList.push_back(input.ki.wVk);
	}
	if (checkList.size() != 0) {
		wcout << "ERRORE! Il comando ricevuto non è ben formato." << endl;
		return;
	}
	
	// Ricava l'handle alla finestra verso cui indirizzare il keystroke
	progHandle = GetForegroundWindow();

	// Send the keystrokes.
	SetForegroundWindow(targetHwnd);
	keystroke_sent = SendInput(vKeysList.size(), &vKeysList[0], sizeof(vKeysList[0]));
	SetForegroundWindow(progHandle);
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

unsigned CALLBACK WindowsNotificationService::hook(void* args)
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

