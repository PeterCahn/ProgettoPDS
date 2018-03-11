#include "Message.h"

class MessageWithTitle : public Message
{
public:

	/* Costruttore */
	MessageWithTitle(operation op, HWND hwnd, wstring windowName);
	/* Construttore di copia */
	MessageWithTitle(const MessageWithTitle & message);
	/* Operatore di assegnazione */
	MessageWithTitle& operator=(const MessageWithTitle& source);

	virtual ~MessageWithTitle();


	BYTE& serialize(u_long& size);
	BYTE& toJson(u_long& size);

protected:
	wstring windowName;

};
