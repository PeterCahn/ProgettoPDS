#pragma once
#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <Winsock2.h>
#include <ws2tcpip.h>
// Link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
// #pragma comment (lib, "Mswsock.lib")

#include <string>

enum operation;

using namespace std;

class Server
{
public:
	Server();
	~Server();

	/* Acquisisce la porta verificando che sia un numero tra 1024 e 65535 */
	int leggiPorta();
	/* Avvia il server facendone il bind ad un socket */
	int avviaServer();
	/* Attende una connessione in entrata da un client */
	int acceptConnection();
	/* Chiude la connessione con il client attualmente connesso */
	void chiudiConnessioneClient();
	/* Chiude il server liberandone le risorse */
	void arrestaServer();

	/* Verifica che il Server abbia fatto effettivamente il bind su di un socket valido */
	bool validServer();
	/* Verifica che il client sia stato accettato con successo e gli sia stato assegnato un socket valido */
	bool validClient();
	/* Invia un messaggio al client in base al valore di 'operation' */
	void sendNotificationToClient(HWND, wstring, operation);
	/* Manda un messaggio breve al client */
	void sendMessageToClient(operation);
	/* Riceve un messaggio dal client */
	int receiveMessageFromClient(char* buffer, int bufferSize);
	/* Legge un numero di bytes definito */
	int readn(SOCKET fd, char* buffer, int n);

private:

	SOCKET clientSocket;	// Gestisce un client alla volta
	SOCKET listeningSocket;	// Il socket del server
	string listeningPort;	// La porta su cui ascoltare connessioni in entrata

	int tentativiAvvioServer;
	bool clientConnected;

	void printMessage(wstring string);

};

