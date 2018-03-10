#include "Message.h"

class MessageWithTitle : public Message
{
public:

	MessageWithTitle(operation op, HWND hwnd, wstring windowName);
	virtual ~MessageWithTitle();

	BYTE& serialize(u_long& size);

	string toJson();
	BYTE& toJson(u_long& size);

protected:
	wstring windowName;

};
