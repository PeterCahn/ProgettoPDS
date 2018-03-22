/* TODO:
- Gestione eccezioni
- Si riesce a terminare l'invio di finestre al client corrente e ad aspettare il prossimo,
	ma non a terminare il server mentre è in attesa sulla accept o sulla lettura della porta.
	Questo avviene SOLO quando si è già provato a chiudere una connessione.
*/

#define UNICODE

#include "WindowsNotificationService.h"
#include "Helper.h"
#include "Message.h"	// qui sono definiti i tipi di operazione
#include "CustomExceptions.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <regex>
#include <io.h>
#include <fcntl.h>
#include <exception>
#include <ShObjIdl.h>

// Per DwmGetWindowAttribute
#include <dwmapi.h>
#pragma comment (lib, "dwmapi.lib")

/* Documentation: https://github.com/nlohmann/json */
#include <nlohmann\json.hpp>
using json = nlohmann::json;

#define DEFAULT_BUFLEN 1024

using namespace std;

/* Per controllare se è stato premuto CTRL+C e quindi il server deve essere terminato */
volatile atomic<bool> isRunning = true;

/* Per uscire dal servizio */
BOOL WINAPI HandlerRoutine(_In_ DWORD dwCtrlType) {
	switch (dwCtrlType)
	{
	case CTRL_C_EVENT:
		isRunning = false;
		// Signal is handled - don't pass it on to the next handler
		return TRUE;
	default:
		// Pass signal on to the next handler
		return FALSE;
	}
}

WindowsNotificationService::WindowsNotificationService()
{
	_setmode(_fileno(stdout), _O_U16TEXT);
		
	numberRetries = 0;
	retry = false;

	windows = map<HWND, wstring>();

	/* Setta la control routine per gestire il CTRL-C: chiude la connessione con il client per rimettersi in attesa */
	if (!SetConsoleCtrlHandler(HandlerRoutine, TRUE))
		throw CtrlCHandlingException("Impossibile settare il control handler.");

}

WindowsNotificationService::~WindowsNotificationService()
{
	/* Sgancia la control routine per gestire il CTRL-C: chiude la connessione con il client per rimettersi in attesa */
	SetConsoleCtrlHandler(HandlerRoutine, FALSE);
}


void WindowsNotificationService::start()
{
	/* Avvia il server */
	try {
		server.leggiPorta();
	}
	catch (ReadPortNumberException&) {
		isRunning = false;
		return;
	}

	/* isRunning: settato a false dall'handler di CTRL-C */
	while (isRunning) {

		try{
			server.avviaServer();

			/* Aspetta nuove connessioni in arrivo e si rimette in attesa se non è possibile accettare la connessione dal client */			
			server.acceptConnection();
		}
		catch (InternalServerStartError) {
			isRunning = false;
			return;
			//isse.getError();
		}
		catch (ReadPortNumberException) {
			isRunning = false;
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

		}
		catch (system_error se) {
			printMessage(TEXT("Errore nella gestione del thread 'notificationsThread'."));

			// Si è verificata un'eccezione nei thread che gestiscono la connessione con il client.
			server.chiudiConnessioneClient();

			return;
		}
		catch (exception)
		{
			// Si è verificata un'eccezione
			server.chiudiConnessioneClient();

			return;
		}

		/* Chiudi connessione con il client prima di provare a reiterare sul while e ad attendere un nuova connessione */
		server.chiudiConnessioneClient();
	}
}

void WindowsNotificationService::stop()
{
	server.arrestaServer();
}

BOOL CALLBACK WindowsNotificationService::EnumWindowsProc(HWND hWnd, LPARAM lParam)
{
	map<HWND, wstring>* windows2 = reinterpret_cast<map<HWND, wstring>*> (lParam);
	wstring windowTitle = Helper::getTitleFromHwnd(hWnd);

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

	// Rimuove alcuni programmi nella tray e "Program Manager"
	ti.cbSize = sizeof(ti);
	GetTitleBarInfo(hwnd, &ti);
	if (ti.rgstate[0] & STATE_SYSTEM_INVISIBLE)
		return FALSE;

	// Non mostrare tool window
	if (GetWindowLong(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
		return FALSE;

	/* Alcune app di Windows 10 vengono lanciate all'avvio del SO oppure rimangono
	 * dopo la loro apparente chiusura in uno stato sospeso in cui non sono visibili,
	 * per essere poi avviate velocemente. Questo stato è detto "cloaked", verifichiamo
	 * che fra quelle restituite da EnumWindowsProc non ce ve siano.
	 * ATTENZIONE: anche le finestre che sono aperte davvero ma non nel desktop virtuale attuale sono segnalate come cloaked da Win10.
	 * GetClassName() ci permette di capire quali sono Windows Store Apps, e poi andiamo a veririficare che la finestra sia davvero su qualche desktop
	 */
	BOOL status;
	DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &status, sizeof(DWMWA_CLOAKED));
	if (status != FALSE) {	// significa che la finestra è cloaked ed è una Windows Store App
		TCHAR className[MAX_PATH];
		GetClassName(hwnd, className, MAX_PATH);
		if (wcscmp(className, L"ApplicationFrameWindow") == 0 || wcscmp(className, L"Windows.UI.Core.CoreWindow") == 0) {
			return hasVirtualDesktop(hwnd);
		}
	}
	return TRUE;
}


/* Verifica che l'applicazione UWP (le Windows Store Apps) sia effettivamente renderizzata su uno dei virtual desktop. 
 * Se non lo è ed è cloaked, significa che è caricata in memoria (per questioni di ottimizzazione)
 * ma non visibile, quindi non è da mostrare. Altrimenti, se ha un virtual desktop, significa che è cloaked solo
 * perchè non è sul desktop corrente, e quindi è da mostrare fra le aperte.
 */
using namespace VirtualDesktops::API;
bool WindowsNotificationService::hasVirtualDesktop(HWND hwnd) {
	::CoInitialize(NULL);

	bool foundAndValid = false;
	IServiceProvider* pServiceProvider = nullptr;
	HRESULT hr = ::CoCreateInstance(CLSID_ImmersiveShell, NULL, CLSCTX_LOCAL_SERVER, __uuidof(IServiceProvider), (PVOID*)&pServiceProvider);
	GUID desktopId = { 0 };

	if (SUCCEEDED(hr)) {
		IVirtualDesktopManager *pDesktopManager = nullptr;
		hr = pServiceProvider->QueryService(__uuidof(IVirtualDesktopManager), &pDesktopManager);

		if (SUCCEEDED(hr))
		{
			hr = pDesktopManager->GetWindowDesktopId(hwnd, &desktopId);
			if (SUCCEEDED(hr) && desktopId != GUID_NULL)
				foundAndValid = true;

			pDesktopManager->Release();
			pDesktopManager = nullptr;
		}
		pServiceProvider->Release();
	}

	return foundAndValid;
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
			this_thread::sleep_for(chrono::milliseconds(250));

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
						printMessage(TEXT("Cambio nome per la finestra:"));
						printMessage(TEXT("- " + previousTitle));
						printMessage(TEXT("Il nuovo nome è:"));
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

			/* Check se è stato premuto CTRL-C (e isRunning è diventato false): 
			 * in caso positivo, manda un messaggio al client per chiudere la connessione
			 */
			if (!isRunning) {
				printMessage(TEXT("Gestione finestre in chiusura..."));
				try {
					server.sendMessageToClient(ERROR_CLOSE);
					server.chiudiConnessioneClient();
					isRunning = true;
				}
				catch (SendMessageException)
				{
					server.chiudiConnessioneClient();
					isRunning = true;
					return;
				}
				return;
			}
		}

	}
	catch (MessageCreationException) {
		isRunning = false;
		return;
	}
	catch (SendMessageException sme) {
		isRunning = false;	// TODO: troppo estremo. Mettere contatore.
		return;
	}
	catch (future_error)
	{
		// Eccezione generata dal future
		printMessage(TEXT("Temporizzazione del thread fallita."));
		server.sendMessageToClient(ERROR_CLOSE);
		server.chiudiConnessioneClient();
		isRunning = false;
		return;
	}
	catch (exception)
	{
		// catch anything thrown within try block that derives from std::exception
		isRunning = false;
		return;
	}
}

void WindowsNotificationService::receiveCommands() {

	// Ricevi finchè il client non chiude la connessione
	char recvbuf[DEFAULT_BUFLEN * sizeof(char)];

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
				stringaRicevuta += '\0';
				j = json::parse(stringaRicevuta);
			}
			catch (json::exception) {
				printMessage(TEXT("JSON ricevuto dal client malformato."));
				continue;
			}
			catch (exception ex)
			{
				printMessage(TEXT("Errore durante il parse del messaggio ricevuto."));
				continue;
			}

			if (j.find("operation") != j.end()) {
				// C'è il campo "operation"
				if (j["operation"] == "CLSCN") {
					printMessage(TEXT("Il client ha richiesta la disconnessione."));
					server.chiudiConnessioneClient();
				}
				else if (j["operation"] == "comando") {
					string virtualKey, stringUpToPlus;
					vector<INPUT> keystroke;

					int tempHwnd = (int)j["hwnd"];
					HWND targetHwnd = (HWND)tempHwnd;

					string keystrokeString = j["tasti"];
					string virtualKeyString;
					for (u_int i = 0; i < keystrokeString.size(); i++) {
						UINT vKey;
						if (keystrokeString[i] == '+') {
							// Aggiungi keyDown
							sscanf_s(virtualKeyString.c_str(), "%u", &vKey);
							INPUT input;
							input.type = INPUT_KEYBOARD;			// Definisce il tipo di input, che può essere INPUT_HARDWARE, INPUT_KEYBOARD o INPUT_MOUSE
																	// Una volta definito il tipo di input come INPUT_KEYBOARD, si usa la sotto-struttura .ki per inserire le informazioni sull'input
							input.ki.wVk = vKey;						// Virtual-key code dell'input.	
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
							input.ki.wVk = vKey;						// Virtual-key code dell'input.	
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

					// Stampa codici virtual-key ricevute
					wcout << "Virtual-key ricevute in invio alla finestra in focus: " << endl; // << stringaRicevuta << std::endl;
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
	int keystroke_sent;
	HWND progHandle;

	// Controlla che il keystroke sia ben formato
	vector<WORD> checkList;
	for each (INPUT input in vKeysList) {
		if (input.ki.dwFlags == KEYEVENTF_KEYUP) {
			// Controlla che il tasto sia stato precedentemente premuto
			vector<WORD>::iterator pos = find(checkList.begin(), checkList.end(), input.ki.wVk);
			if (pos == checkList.end()) {
				printMessage(L"ERRORE! Il comando ricevuto non è ben formato.");
				return;
			}
			// Se cè, rimuovilo dalla lista
			checkList.erase(pos);

		}
		else if (input.ki.dwFlags == 0)
			checkList.push_back(input.ki.wVk);
	}
	if (checkList.size() != 0) {
		printMessage(L"ERRORE! Il comando ricevuto non è ben formato.");
		return;
	}

	// Ricava l'handle alla finestra attualmente in focus, che non è necessariamente coincidente con targetHwnd
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
