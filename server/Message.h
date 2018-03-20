#include "stdafx.h"
#include <typeinfo>
#include <string>

/* Documentation: https://github.com/nlohmann/json */
#include <nlohmann\json.hpp>

enum operation {
	OPEN,
	CLOSE,
	FOCUS,
	TITLE_CHANGED,
	ERROR_CLOSE
};

using namespace std;

class Message
{
public:

	/* Costruttori overloaded */
	Message(operation operation);
	Message(operation op, HWND hwnd);

	/* Construttore di copia */
	Message(const Message&);
	/* Operatore di assegnazione */
	Message& operator=(const Message& source);
		
	/* Avendo abilitato il comportamento polimorfico (vedi metodi sotto), anche il destructor deve essere 'virtual' */
	virtual ~Message();

	/* La parola chiave 'virtual' abilita il comportamento polimorfico */
	virtual BYTE& serialize(u_long& size);
	virtual BYTE& toJson(u_long& size);

protected:
	HWND hwnd;
	operation op;	

	BYTE* buffer;
	long bufferSize;

};

