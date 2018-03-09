#define UNICODE

#include "MessageWithIcon.h"
#include "base64.h"

using json = nlohmann::json;

#include <vector>

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

MessageWithIcon::MessageWithIcon(operation op, HWND hwnd, wstring windowName, BYTE& icona, u_long iconLength)
	: MessageWithTitle(op, hwnd, windowName)
{
	this->pixels = &icona;
	this->iconLength = iconLength;		
}


MessageWithIcon::~MessageWithIcon()
{
	if (pixels != NULL)
		delete[] pixels;
}

BYTE & MessageWithIcon::serialize(u_long & size)
{
	TCHAR progName[MAX_PATH * sizeof(TCHAR)];
	//ZeroMemory(windowName, MAX_PATH * sizeof(wchar_t));

	/* Copia in progName la stringa ottenuta */
	wcscpy_s(progName, windowName.c_str());

	char dimension[MSG_LENGTH_SIZE];	// 2 trattini, 4 byte per la dimensione e trattino
	char operation[N_BYTE_OPERATION + N_BYTE_TRATTINO];	// 5 byte per l'operazione e trattino + 1	

	u_long progNameLength = windowName.length() * sizeof(TCHAR);
	u_long netProgNameLength = htonl(progNameLength);

	u_long msgLength = MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE +
		PROG_NAME_LENGTH + progNameLength + N_BYTE_TRATTINO + ICON_LENGTH_SIZE + iconLength;
	u_long netMsgLength = htonl(msgLength);

	size = msgLength;

	memcpy(dimension, "--", 2 * N_BYTE_TRATTINO);
	memcpy(dimension + 2 * N_BYTE_TRATTINO, (void*)&netMsgLength, N_BYTE_MSG_LENGTH);
	memcpy(dimension + 2 * N_BYTE_TRATTINO + N_BYTE_MSG_LENGTH, "-", N_BYTE_TRATTINO);

	/* Salva l'operazione */
	memcpy(operation, "OPENP-", N_BYTE_OPERATION + N_BYTE_TRATTINO);

	/* Crea buffer da inviare */
	buffer = new BYTE[MSG_LENGTH_SIZE + msgLength];

	memcpy(buffer, dimension, MSG_LENGTH_SIZE);	// Invia prima la dimensione "--<b1,b2,b3,b4>-" (7 byte)

	memcpy(buffer + MSG_LENGTH_SIZE, operation, OPERATION_SIZE);	// "<operation>-"	(6 byte)

	memcpy(buffer + MSG_LENGTH_SIZE + OPERATION_SIZE, &hwnd, N_BYTE_HWND);
	memcpy(buffer + MSG_LENGTH_SIZE + OPERATION_SIZE + N_BYTE_HWND, "-", N_BYTE_TRATTINO);	// Aggiungi trattino (1 byte)

	memcpy(buffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE, &netProgNameLength, N_BYTE_PROG_NAME_LENGTH);	// Aggiungi lunghezza progName (4 byte)
	memcpy(buffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + N_BYTE_PROG_NAME_LENGTH, "-", N_BYTE_TRATTINO);	// Aggiungi trattino (1 byte)
	memcpy(buffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH, progName, progNameLength);	// <progName>
	memcpy(buffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH + progNameLength, "-", N_BYTE_TRATTINO);	// Aggiungi trattino (1 byte)

	memcpy(buffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH + progNameLength + N_BYTE_TRATTINO, &iconLength, N_BYTE_ICON_LENGTH);	// Aggiungi dimensione icona (4 byte)
	memcpy(buffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH + progNameLength + N_BYTE_TRATTINO + N_BYTE_ICON_LENGTH, "-", N_BYTE_TRATTINO);	// Aggiungi trattino (1 byte)
	memcpy(buffer + MSG_LENGTH_SIZE + OPERATION_SIZE + HWND_SIZE + PROG_NAME_LENGTH + progNameLength + N_BYTE_TRATTINO + ICON_LENGTH_SIZE, pixels, iconLength);	// Aggiungi dati icona

	return *buffer;
}


BYTE& MessageWithIcon::toJson(u_long& size)
{
	TCHAR progName[MAX_PATH * sizeof(TCHAR)];
	//ZeroMemory(windowName, MAX_PATH * sizeof(wchar_t));

	/* Copia in progName la stringa ottenuta */
	wcscpy_s(progName, windowName.c_str());

	json j;

	j["operation"] = "OPEN";
	j["hwnd"] = (unsigned int)hwnd;

	j["windowName"] = base64_encode(reinterpret_cast<const unsigned char*>(progName), windowName.length()*sizeof(TCHAR));

	j["iconLength"] = iconLength;

	j["icona"] = base64_encode(reinterpret_cast<const unsigned char*>(pixels), iconLength);

	string s = j.dump();
	//string base64 = base64_encode(reinterpret_cast<const unsigned char*>(s.c_str()),s.length());

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