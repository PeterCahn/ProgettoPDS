#include "MessageWithTitle.h"

class MessageWithIcon : public MessageWithTitle
{
public:

	/* Costruttore */
	MessageWithIcon(operation op, HWND hwnd, wstring windowName, BYTE& icona, u_long iconLength);
	/* Construttore di copia */
	MessageWithIcon(const MessageWithIcon & message);
	/* Operatore di assegnazione */
	MessageWithIcon& operator=(const MessageWithIcon& source);

	~MessageWithIcon();

	BYTE& serialize(u_long& size);

	BYTE& toJson(u_long& size);

protected:	
	BYTE* pixels;
	u_long iconLength;
	
};

