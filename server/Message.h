#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <ws2tcpip.h>
#include <typeinfo>
#include <string>

enum operation {
	OPEN,
	CLOSE,
	FOCUS,
	TITLE_CHANGED
};

using namespace std;

class Message
{
public:

	Message(operation op, HWND hwnd);	

	/* Avendo abilitato il comportamento polimorfico, anche il destructor deve essere 'virtual' */
	virtual ~Message();

	/* La parola chiave 'virtual' abilita il comportamento polimorfico */
	virtual BYTE& serialize(u_long& size);

protected:
	HWND hwnd;
	operation op;	

	BYTE* buffer;

};

