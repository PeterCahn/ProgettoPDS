#include "Message.h"

class MessageWithTitle : public Message
{
public:

	MessageWithTitle(operation op, HWND hwnd, wstring windowName);
	~MessageWithTitle();

	BYTE& serialize(u_long& size);

protected:
	wstring windowName2;

};
