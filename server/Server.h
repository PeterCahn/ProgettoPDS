#include <Windows.h>
#include <string>
#include <typeinfo>

enum operation;

using namespace std;

class Server
{
public:
	Server();
	~Server();

	/* Acquisisce la porta verificando che sia un numero tra 1024 e 65535 */
	void leggiPorta();
	/* Avvia il server facendone il bind ad un socket */
	void avviaServer();
	/* Attende una connessione in entrata da un client */
	void acceptConnection();
	/* Chiude la connessione con il client attualmente connesso */
	void chiudiConnessioneClient();
	/* Chiude il server liberandone le risorse */
	void arrestaServer();

	/* Verifica che il Server abbia fatto effettivamente il bind su di un socket valido */
	bool validServer();
	/* Verifica che il client sia stato accettato con successo e gli sia stato assegnato un socket valido */
	bool validClient();
	/* Invia un messaggio al client in base al valore di 'operation' */
	void sendNotificationToClient(HWND hwnd, wstring title, operation op);
	/* Riceve un messaggio dal client */
	int receiveMessageFromClient(char* buffer, int bufferSize);

	SOCKET getClientSocket();
	SOCKET getListeningSocket();

private:

	SOCKET clientSocket;	// Gestisce un client alla volta
	SOCKET listeningSocket;	// Il socket del server
	string listeningPort;	// La porta su cui ascoltare connessioni in entrata

	void printMessage(wstring string);

};

