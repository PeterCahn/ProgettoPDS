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
	/* OPENP */
	Message(operation op, HWND hwnd, wstring windowName, BYTE& icona, u_long iconLength);
	/* FOCUS and CLOSE */
	Message(operation op, HWND hwnd);
	/* TITLE_CHANGED */
	Message(operation op, HWND hwnd, wstring windowName);

	~Message();

	BYTE& serialize(u_long& size);

private:
	HWND hwnd;
	operation op;
	wstring windowName;
	BYTE* buffer;
	BYTE* pixels;
	u_long iconLength;

	BYTE& serializeFocusOrClose(operation oper, u_long& size);
	BYTE& serializeTitleChanged(u_long& size);
	BYTE& serializeOpen(u_long& size);

};

