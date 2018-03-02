#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <ws2tcpip.h>
#include <typeinfo>
#include <string>


enum operation;

using namespace std;

class Message
{
public:
	/* TODO: FOCUS and CLOSE (senza windowName) */
	Message(operation op, HWND hwnd);

	/* FOCUS and CLOSE (con windowName) */
	Message(operation op, HWND hwnd, wstring windowName);

	~Message();

	virtual BYTE& serialize(u_long& size);

protected:
	HWND hwnd;
	operation op;
	wstring windowName;	// TODO: da eliminare

	BYTE* buffer;


};

