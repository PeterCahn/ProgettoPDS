#define UNICODE

#include "Message.h"
#include "base64.h"

// for convenience
using json = nlohmann::json;

using namespace std;

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

Message::Message(operation op, HWND hwnd)
{
	this->op = op;
	this->hwnd = hwnd;
}

Message::Message(operation op)
{
	this->op = op;
}

Message::Message(const Message& message) 
{
	this->op = message.op;
	if(message.hwnd != 0)
		this->hwnd = message.hwnd;

	if (this->buffer != nullptr) {							// check che sia stato allocato il buffer, altrimenti non fare delete
		this->buffer = new BYTE[message.bufferSize];
		memcpy(this->buffer, message.buffer, message.bufferSize);
	}
}

Message& Message::operator=(const Message& source) {
	if (this != &source) {									// per non assegnare un oggetto a s� stesso
		delete[] this->buffer;
		this->buffer = nullptr;								// per evitare che in caso di eccezione la memoria venga rilasciata due volte
		this->bufferSize = source.bufferSize;
		this->buffer = new BYTE[bufferSize];
		memcpy(this->buffer, source.buffer, bufferSize);
	}
	return *this;
}


Message::~Message()
{
	if (this->buffer != NULL) {
		delete[] this->buffer;
	}
}

BYTE& Message::serialize(u_long& size)
{
	char dimension[MSG_LENGTH_SIZE];	// 2 trattini, 4 byte per la dimensione e trattino
	char operation[N_BYTE_OPERATION + N_BYTE_TRATTINO];	// 5 byte per l'operazione e trattino + 1

	/* Calcola lunghezza totale messaggio e salvala */
	u_long msgLength = MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE;
	u_long netMsgLength = htonl(msgLength);

	size = msgLength;
	bufferSize = size;

	memcpy(dimension, "--", 2);
	memcpy(dimension + 2, (void*)&netMsgLength, 4);
	memcpy(dimension + 6, "-", 1);

	if (op == FOCUS)
		memcpy(operation, "FOCUS-", 6);
	else if (op == CLOSE)
		memcpy(operation, "CLOSE-", 6);

	/* Inizializza buffer per il messaggio */
	buffer = new BYTE[MSG_LENGTH_SIZE + msgLength];

	memcpy(buffer, dimension, MSG_LENGTH_SIZE);	// Invia prima la dimensione "--<b1,b2,b3,b4>-" (7 byte)

	memcpy(buffer + MSG_LENGTH_SIZE, operation, OPERATION_SIZE);	// "<operation>-"	(6 byte)

	memcpy(buffer + MSG_LENGTH_SIZE + OPERATION_SIZE, &hwnd, N_BYTE_HWND);

	return *buffer;
}

BYTE& Message::toJson(u_long& size)
{
	json j;

	if (op == FOCUS)
		j["operation"] = "FOCUS";
	else if (op == CLOSE)
		j["operation"] = "CLOSE";
	else if (op == ERROR_CLOSE)
		j["operation"] = "ERRCL";
	else if (op == OK_CLOSE)
		j["operation"] = "OKCLO";

	if (op == FOCUS || op == CLOSE) {
		j["hwnd"] = (unsigned int) hwnd;
	}
	
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
	buffer = new BYTE[MSG_LENGTH_SIZE + msgLength];

	memcpy(buffer, dimension, MSG_LENGTH_SIZE);	// Invia prima la dimensione "--<b1,b2,b3,b4>-" (7 byte)
	memcpy(buffer + MSG_LENGTH_SIZE, s.c_str(), size);

	return *buffer;
}
