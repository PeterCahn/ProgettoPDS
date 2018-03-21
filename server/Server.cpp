#pragma once
#define UNICODE

#include "Server.h"
#include "Helper.h"
#include "MessageWithIcon.h"

#include <iostream>
#include <regex>
#include <exception>

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

Server::Server()
{
	WSADATA wsaData;

	// Inizializza Winsock
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {		
		wcout << "[" << GetCurrentThreadId() << "] " << "ServerClass non inizializzata correttamente." << endl;
		return;
	}

	retryInputPort = false;
}

Server::~Server()
{
	/* Se non � stato ancora chiuso il client socket, chiudilo insieme alla connessione con il client */
	chiudiConnessioneClient();

	/* Arresta il server rilasciando le sue risorse (close del listeningSocket) */
	arrestaServer();

	/* Termina l'uso della Winsock 2 DLL (Ws2_32.dll) */
	WSACleanup();
}

/* Per uscire dal servizio */
volatile bool runningServer = true;
BOOL WINAPI StopServer(_In_ DWORD dwCtrlType) {
	switch (dwCtrlType)
	{
	case CTRL_C_EVENT:
		runningServer = false;
		// Signal is handled - don't pass it on to the next handler
		return TRUE;
	default:
		// Pass signal on to the next handler
		return FALSE;
	}
}

/* Acquisisce la porta verificando che sia un numero tra 1024 e 65535 */
int Server::leggiPorta()
{
	printMessage(TEXT("Inserire la porta su cui ascoltare: "));

	/* Ottieni porta su cui ascoltare */
	string porta;
	regex portRegex("102[4-9]|10[3-9][0-9]|11[0-9][0-9]|[2-9][0-9][0-9][0-9]|[1-5][0-9][0-9][0-9][0-9]|6[0-4][0-9][0-9][0-9]|65[0-5][0-9][0-9]|655[0-3][0-9]|6553[0-5]");
	while (true)
	{
		cin >> porta;
		
		// Se � stato premuto CTRL-C, viene settato runningServer a false, l'input viene terminato e si esce
		if (!runningServer )
			return -1;

		// Se non � stato premuto CTRL-C, ma ci sono problemi con cin, ritorna.
		if (!cin.good()) {
			printMessage(TEXT("Errore nella lettura. Riprovare."));
			return -1;
		}
		else if (!regex_match(porta, portRegex)) {	// Porta inserita non soddisfa la regex
			printMessage(TEXT("Intervallo ammesso per il valore della porta: [1024-65535]"));
		}
		else	// Tutto � andato a buon fine, porta letta, ora esci dal ciclo con il break
			break;
		
	}
	listeningPort = porta;
	
	return 0;
}

/* Avvia il server settando la listeningPort */
int Server::avviaServer()
{
	int iResult;	
	listeningSocket = INVALID_SOCKET;

	while (true) 
	{
		// Creazione socket
		listeningSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listeningSocket == INVALID_SOCKET) {
			wcout << "[" << GetCurrentThreadId() << "] " << "socket() fallita con errore: " << WSAGetLastError() << endl;			
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

			if (errorCode == WSAEADDRINUSE) {
				wcout << "[" << GetCurrentThreadId() << "] " << "Porta " << atoi(listeningPort.c_str()) << " gi� in uso. Scegliere un'altra porta." << endl;

				if (leggiPorta() < 0) {
					return -1;
				}
				continue;
			}

			closesocket(listeningSocket);
			listeningSocket = INVALID_SOCKET;
			continue;
		}

		// Ascolta per richieste di connessione
		iResult = listen(listeningSocket, SOMAXCONN);
		if (iResult == SOCKET_ERROR) {
			wcout << "[" << GetCurrentThreadId() << "] " << "listen() fallita con errore: " << WSAGetLastError() << endl;
			closesocket(listeningSocket);
			listeningSocket = INVALID_SOCKET;
			continue;
		}

		if (validServer())	// server avviato con successo: break
			break;
	}

	return 0;
}

int CALLBACK checkRunningServer(
	IN LPWSABUF lpCallerId,
	IN LPWSABUF lpCallerData,
	IN OUT LPQOS lpSQOS,
	IN OUT LPQOS lpGQOS,
	IN LPWSABUF lpCalleeId,
	OUT LPWSABUF lpCalleeData,
	OUT GROUP FAR *g,
	IN DWORD_PTR dwCallbackData
)
{
	if (runningServer)
		return CF_ACCEPT;	
	else
		return CF_REJECT;
}

/* Attende una connessione in entrata da un client e setta il clientSocket */
int Server::acceptConnection()
{
	int iResult = 0;
	SOCKET newClientSocket;

	/* Setta la control routine per gestire il CTRL-C: chiude il server */
	/*
	if (!SetConsoleCtrlHandler(StopServer, TRUE)) {
		printMessage(TEXT("ERRORE: Impossibile settare il control handler."));
		return -1;
	}
	*/
	while (runningServer) {		

		try {

			if (!runningServer)
				return -1;

			printMessage(TEXT("In attesa della connessione di un client..."));

			struct sockaddr_in clientSockAddr;			
			int nameLength = sizeof(clientSockAddr);

			newClientSocket = WSAAccept(listeningSocket, (SOCKADDR*)&clientSockAddr, &nameLength, checkRunningServer, NULL);
			if (newClientSocket == INVALID_SOCKET && !runningServer)
				return -1;
			/*
			// Accetta la connessione
			newClientSocket = accept(listeningSocket, NULL, NULL);
			if (newClientSocket == INVALID_SOCKET) {
				wcout << "[" << GetCurrentThreadId() << "] " << "accept() fallita con errore: " << WSAGetLastError() << endl;
				newClientSocket = INVALID_SOCKET;
				return 0;
			}
			*/

			getpeername(newClientSocket, reinterpret_cast<struct sockaddr*>(&clientSockAddr), &nameLength);
			int port = ntohs(clientSockAddr.sin_port);

			char ipstr[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &clientSockAddr.sin_addr, ipstr, INET_ADDRSTRLEN);

			wcout << "[" << GetCurrentThreadId() << "] " << "Connessione stabilita con " << ipstr << ":" << port << std::endl;

			clientSocket = newClientSocket;

			closesocket(listeningSocket);

			if (validClient() && runningServer)
				break;
		}
		catch (exception& ex) {
			wcout << "[" << GetCurrentThreadId() << "] " << "Eccezione lanciata durante l'accettazione del client: " << ex.what() << endl;
		}
		/*
		if (!SetConsoleCtrlHandler(StopServer, FALSE)) {
			printMessage(TEXT("ERRORE: Impossibile settare il control handler."));
			return 0;
		}
		*/
	}

	return 0;
}

void Server::chiudiConnessioneClient()
{
	if(validClient())
		closesocket(clientSocket);
}

void Server::arrestaServer()
{
	closesocket(listeningSocket);
}

/*
* Invia il nome della finestra e l'informazione ad esso associata al client
* Il formato del messaggio per le operazioni CLOSE e FOCUS � :
*		--<operazione>-<lunghezza_nome_finestra>-<nomefinestra>
*
* Se l'operazione � OPEN, aggiunge al precedente formato la lunghezza del file contenente l'icona
* seguito dal file stesso secondo il seguente formato:
*		--<operazione>-<lunghezza_nome_finestra>-<nomefinestra>-<lunghezza_file_icona>-<dati_file_icona_bmp>
*/
void Server::sendNotificationToClient(HWND hwnd, wstring title, operation op) {
	
	u_long msgLength = 0;
	Message* message = nullptr;
	BYTE* lpPixels = nullptr;
	
	try {

		if (op == OPEN) {
			/* Ottieni l'icona */
			u_long iconLength = 0;

			BYTE& pixels = Helper::ottieniIcona(hwnd, iconLength);
			
			message = new MessageWithIcon(op, hwnd, title, pixels, iconLength);
		}
		else if (op == FOCUS || op == CLOSE) {
			message = new Message(op, hwnd);
		}
		else if (op == TITLE_CHANGED) {
			message = new MessageWithTitle(op, hwnd, title);

		}

		/* Restituisce la reference al buffer da inviare e riempie msgLength con la dimensione del messaggio */
//		BYTE& buffer = message->serialize(msgLength);
		BYTE& buffer = message->toJson(msgLength);

		int bytesSent = 0;
		int offset = 0;
		int remaining = MSG_LENGTH_SIZE + msgLength;
		while (remaining > 0)
		{
			bytesSent = send(clientSocket, (char*)&buffer, remaining, offset);
			if (bytesSent < 0)
				break;
			remaining -= bytesSent;
			offset += bytesSent;
		}

		delete message;
	}
	catch (exception&)
	{
		if (lpPixels != NULL)
			delete[] lpPixels;

		if (message != NULL)
			delete message;		// TODO: lancia un'eccezione se si raggiunge il catch

		// Rilancia l'eccezione perch� venga gestita nei livelli superiori
		throw;
	}

	return;
}

void Server::sendMessageToClient(operation op) {

	try {

		Message message = Message(op);
		
		u_long msgLength = 0;
		BYTE& buffer = message.toJson(msgLength);
		
		int bytesSent = 0;
		int offset = 0;
		int remaining = MSG_LENGTH_SIZE + msgLength;
		while (remaining > 0)
		{
			bytesSent = send(clientSocket, (char*)&buffer, remaining, offset);
			if (bytesSent < 0)
				return;
			remaining -= bytesSent;
			offset += bytesSent;
		}
	}
	catch (exception&)
	{
		// Rilancia l'eccezione perch� venga gestita nei livelli superiori
		throw;
	}

}

int Server::receiveMessageFromClient(char* buffer, int bufferSize)
{
	int iResult = recv(clientSocket, buffer, bufferSize, 0);

	if (iResult > 0)
		return iResult;
	if (iResult == 0) {
		printMessage(TEXT("Connessione chiusa."));
	}
	else if (iResult < 0) {
		int errorCode = WSAGetLastError();
		if (errorCode == WSAECONNRESET) {
			printMessage(TEXT("Connessione chiusa dal client."));
		}
		else
			printMessage(TEXT("Errore durante la ricezione dei dati."));			
	}

	return iResult;

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
