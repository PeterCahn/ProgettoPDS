#pragma once
#define UNICODE

#include "Server.h"
#include "Helper.h"
#include "MessageWithIcon.h"
#include "CustomExceptions.h"

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

#define MAX_TENTATIVI_RIAVVIO_SERVER 3

Server::Server()
{
	WSADATA wsaData;

	// Inizializza Winsock
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		wcout << "[" << GetCurrentThreadId() << "] " << "ServerClass non inizializzata correttamente." << endl;
		return;
	}

	tentativiAvvioServer = 0;
}

Server::~Server()
{
	/* Se non è stato ancora chiuso il client socket, chiudilo insieme alla connessione con il client */
	chiudiConnessioneClient();

	/* Arresta il server rilasciando le sue risorse (close del listeningSocket) */
	arrestaServer();

	/* Termina l'uso della Winsock 2 DLL (Ws2_32.dll) */
	WSACleanup();
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

		// Se non è stato premuto CTRL-C, ma ci sono problemi con cin, ritorna.
		if (!cin.good()) {
			throw ReadPortNumberException("Impossibile settare la porta.");
		}
		else if (!regex_match(porta, portRegex)) {	// Porta inserita non soddisfa la regex
			printMessage(TEXT("Intervallo ammesso per il valore della porta: [1024-65535]"));
		}
		else	// Tutto è andato a buon fine, porta letta, ora esci dal ciclo con il break
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
		try {
			// Creazione socket
			listeningSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (listeningSocket == INVALID_SOCKET) {
				throw InternalServerStartError("socket() fallita con errore.", WSAGetLastError());
			}

			int iOptval = 1;
			iResult = setsockopt(listeningSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *)&iOptval, sizeof(iOptval));
			if (iResult == SOCKET_ERROR) {
				throw InternalServerStartError("setsockopt for SO_EXCLUSIVEADDRUSE failed with error", WSAGetLastError());
			}
			/*
			iResult = setsockopt(listeningSocket, SOL_SOCKET, SO_CONDITIONAL_ACCEPT, (char *)&iOptval, sizeof(iOptval));
			if (iResult == SOCKET_ERROR) {
				throw InternalServerStartError("setsockopt for SO_EXCLUSIVEADDRUSE failed with error", WSAGetLastError());
			}
			*/
			// Imposta struct sockaddr_in
			struct sockaddr_in mySockaddr_in;
			mySockaddr_in.sin_addr.s_addr = htonl(INADDR_ANY);
			mySockaddr_in.sin_port = htons(atoi(listeningPort.c_str()));
			mySockaddr_in.sin_family = AF_INET;

			// Associa socket a indirizzo locale
			iResult = ::bind(listeningSocket, reinterpret_cast<struct sockaddr*>(&mySockaddr_in), sizeof(mySockaddr_in));
			if (iResult == SOCKET_ERROR) {
				int errorCode = WSAGetLastError();

				/* Se la porta è già in uso, chiedi di inserire un'altra porta */
				if (errorCode == WSAEADDRINUSE) {
					wcout << "[" << GetCurrentThreadId() << "] " << "Porta " << atoi(listeningPort.c_str()) << " già in uso. Scegliere un'altra porta." << endl;

					if (leggiPorta() < 0) {
						throw ReadPortNumberException("Impossibile settare la porta al server.");
					}
					else
						continue;
				}
				else
					throw InternalServerStartError("bind() fallita con errore.", WSAGetLastError());
			}

			// Ascolta per richieste di connessione
			iResult = listen(listeningSocket, 1);
			if (iResult == SOCKET_ERROR) {
				throw InternalServerStartError("listen() fallita con errore.", WSAGetLastError());
			}
		}
		catch (InternalServerStartError isse) {

			closesocket(listeningSocket);
			listeningSocket = INVALID_SOCKET;

			tentativiAvvioServer++;
			if (tentativiAvvioServer < MAX_TENTATIVI_RIAVVIO_SERVER)
				continue;
			else	// notifica ai livelli successivi solo se non è possibile avviare il server dopo MAX_TENTATIVI_RIAVVIO_SERVER
				throw InternalServerStartError("Tentativo di avviare il server fallito.", -1);

		}
		catch (ReadPortNumberException &rpne) {
			throw rpne;		// notifica ai livelli successivi
		}

		if (validServer())	// server avviato con successo: break
			break;
	}

	return 0;
}

/* Attende una connessione in entrata da un client e setta il clientSocket */
int Server::acceptConnection()
{
	int iResult = 0;
	SOCKET newClientSocket;

	while (true) {

		try {

			printMessage(TEXT("In attesa della connessione di un client..."));

			struct sockaddr_in clientSockAddr;
			int nameLength = sizeof(clientSockAddr);

			// Accetta la connessione
			newClientSocket = WSAAccept(listeningSocket, (SOCKADDR*)&clientSockAddr, &nameLength, NULL, NULL);
			//newClientSocket = accept(listeningSocket, NULL, NULL);
			if (newClientSocket == INVALID_SOCKET) 
				throw InternalServerStartError("accept() fallita con errore.", WSAGetLastError());			
			
			getpeername(newClientSocket, reinterpret_cast<struct sockaddr*>(&clientSockAddr), &nameLength);
			int port = ntohs(clientSockAddr.sin_port);

			char ipstr[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &clientSockAddr.sin_addr, ipstr, INET_ADDRSTRLEN);

			wcout << "[" << GetCurrentThreadId() << "] " << "Connessione stabilita con " << ipstr << ":" << port << endl;
			wcout << "[" << GetCurrentThreadId() << "] " << "Per terminare la connessione con il client premere CTRL-C." << endl;

			clientSocket = newClientSocket;
			closesocket(listeningSocket);

			if (validClient())
				break;
		}
		catch (InternalServerStartError& isse)
		{
			throw isse;
		}
		catch (exception& ex) {
			throw ex;
		}
	}

	return 0;
}

void Server::chiudiConnessioneClient()
{
	if (validClient())
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
*/
void Server::sendNotificationToClient(HWND hwnd, wstring title, operation op) {

	u_long msgLength = 0;
	Message* message = nullptr;
	BYTE* lpPixels = nullptr;
	int retryTimes = 0;

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
	BYTE& buffer = message->toJson(msgLength);

	int bytesSent = 0;
	int offset = 0;
	int remaining = MSG_LENGTH_SIZE + msgLength;
	while (remaining > 0)
	{
		bytesSent = send(clientSocket, (char*)&buffer, remaining, offset);
		if (bytesSent < 0)
			throw SendMessageException("send() della notifica fallita con errore", bytesSent);
		remaining -= bytesSent;
		offset += bytesSent;
	}

	delete message;

	return;
}

void Server::sendMessageToClient(operation op) {

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
			throw SendMessageException("send() del messaggio fallita con errore", bytesSent);
		remaining -= bytesSent;
		offset += bytesSent;
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
	return !(listeningSocket == INVALID_SOCKET);
}

bool Server::validClient()
{
	return !(clientSocket == INVALID_SOCKET);
}
