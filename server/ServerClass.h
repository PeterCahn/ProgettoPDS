#include <Windows.h>
#include <string>
#include <typeinfo>

using namespace std;

class ServerClass
{
public:
	ServerClass();
	~ServerClass();

	void leggiPorta();
	void avviaServer();
	void acceptConnection();

	bool validServer();
	bool validClient();
	SOCKET getClientSocket();
	SOCKET getListeningSocket();

private:

	SOCKET clientSocket;	// Gestisce un client alla volta
	SOCKET listeningSocket;	// Il socket del server
	string listeningPort;	// La porta su cui ascoltare connessioni in entrata

};

