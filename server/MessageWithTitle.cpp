#define UNICODE

#include "MessageWithTitle.h"
#include "base64.h"

using json = nlohmann::json;

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

MessageWithTitle::MessageWithTitle(operation op, HWND hwnd, wstring windowName) : Message(op, hwnd)
{	
	this->windowName = windowName;
}

MessageWithTitle::MessageWithTitle(const MessageWithTitle & message) : Message(message)
{
	this->windowName = message.windowName;
}

MessageWithTitle & MessageWithTitle::operator=(const MessageWithTitle & source)
{
	if (this != &source) {									// per non assegnare un oggetto a s� stesso
		if (this->buffer != nullptr) {
			delete[] this->buffer;
			this->buffer = nullptr;								// per evitare che in caso di eccezione la memoria venga rilasciata due volte
			this->bufferSize = source.bufferSize;
			this->buffer = new BYTE[bufferSize];
			memcpy(this->buffer, source.buffer, bufferSize);
		}

		this->op = source.op;
		this->hwnd = source.hwnd;
		this->windowName = source.windowName;
	}
	return *this;
}

MessageWithTitle::~MessageWithTitle()
{

}

BYTE& MessageWithTitle::serialize(u_long& size)
{
	TCHAR progName[MAX_PATH * sizeof(TCHAR)];
	//ZeroMemory(windowName, MAX_PATH * sizeof(wchar_t));

	/* Copia in progName la stringa ottenuta */
	wcscpy_s(progName, windowName.c_str());

	char dimension[MSG_LENGTH_SIZE];	// 2 trattini, 4 byte per la dimensione e trattino
	char operation[N_BYTE_OPERATION + N_BYTE_TRATTINO];	// 5 byte per l'operazione e trattino + 1

	u_long progNameLength = windowName.length() * sizeof(TCHAR);
	u_long netProgNameLength = htonl(progNameLength);

	/* Calcola lunghezza totale messaggio e salvala */
	u_long msgLength = MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH + progNameLength;
	u_long netMsgLength = htonl(msgLength);

	size = msgLength;

	memcpy(dimension, "--", 2);
	memcpy(dimension + 2, (void*)&netMsgLength, 4);
	memcpy(dimension + 6, "-", 1);

	memcpy(operation, "TTCHA-", 6);

	/* Crea buffer da inviare */
	bufferSize = MSG_LENGTH_SIZE + msgLength;
	buffer = new BYTE[bufferSize];

	memcpy(buffer, dimension, MSG_LENGTH_SIZE);	// Invia prima la dimensione "--<b1,b2,b3,b4>-" (7 byte)

	memcpy(buffer + MSG_LENGTH_SIZE, operation, OPERATION_SIZE);	// "<operation>-"	(6 byte)

	memcpy(buffer + MSG_LENGTH_SIZE + OPERATION_SIZE, &hwnd, N_BYTE_HWND);
	memcpy(buffer + MSG_LENGTH_SIZE + OPERATION_SIZE + N_BYTE_HWND, "-", N_BYTE_TRATTINO);	// Aggiungi trattino (1 byte)

	memcpy(buffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE, &netProgNameLength, N_BYTE_PROG_NAME_LENGTH);	// Aggiungi lunghezza progName (4 byte)
	memcpy(buffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + N_BYTE_PROG_NAME_LENGTH, "-", N_BYTE_TRATTINO);	// Aggiungi trattino (1 byte)
	memcpy(buffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH, progName, progNameLength);	// <progName>

	return *buffer;
}

BYTE& MessageWithTitle::toJson(u_long& size)
{
	json j;
	TCHAR progName[MAX_PATH * sizeof(TCHAR)];
	//ZeroMemory(windowName, MAX_PATH * sizeof(wchar_t));

	/* Copia in progName la stringa ottenuta */
	wcscpy_s(progName, windowName.c_str());
		
	j["operation"] = "TTCHA";
	j["hwnd"] = (unsigned int)hwnd;

	j["windowName"] = base64_encode(reinterpret_cast<const unsigned char*>(progName), windowName.length() * sizeof(TCHAR));
	
	string s = j.dump();
	//string base64 = base64_encode(reinterpret_cast<const unsigned char*>(s.c_str()), s.length());
	
	char dimension[MSG_LENGTH_SIZE];	// 2 trattini, 4 byte per la dimensione e trattino	

	/* Calcola lunghezza totale messaggio e salvala */
	u_long msgLength = s.length();
	u_long netMsgLength = htonl(msgLength);

	size = msgLength;

	memcpy(dimension, "--", 2);
	memcpy(dimension + 2, (void*)&netMsgLength, 4);
	memcpy(dimension + 6, "-", 1);

	/* Inizializza buffer per il messaggio */
	bufferSize = MSG_LENGTH_SIZE + msgLength;
	buffer = new BYTE[bufferSize];

	memcpy(buffer, dimension, MSG_LENGTH_SIZE);	// Invia prima la dimensione "--<b1,b2,b3,b4>-" (7 byte)
	memcpy(buffer + MSG_LENGTH_SIZE, s.c_str(), size);

	return *buffer;
}