#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <ws2tcpip.h>
#include <typeinfo>
#include <string>

/* Documentation: https://github.com/nlohmann/json */
#include <nlohmann\json.hpp>

enum operation {
	OPEN,
	CLOSE,
	FOCUS,
	TITLE_CHANGED,
	ERROR_CLOSE,
	OK_CLOSE
};

using namespace std;

class Message
{
public:

	Message(operation op, HWND hwnd);
	Message(operation operation);
		
	/* TODO: La regola dei tre.
		Reminder. Se una classe dispone di una qualunque di queste funzioni membro, occorre implementare le altre due.
		- Costruttore di copia
		- Operatore di assegnazione
		- Distruttore
	*/
	/* Avendo abilitato il comportamento polimorfico, anche il destructor deve essere 'virtual' */
	virtual ~Message();

	/* La parola chiave 'virtual' abilita il comportamento polimorfico */
	virtual BYTE& serialize(u_long& size);

	virtual BYTE& toJson(u_long& size);

protected:
	HWND hwnd;
	operation op;	

	BYTE* buffer;

};

