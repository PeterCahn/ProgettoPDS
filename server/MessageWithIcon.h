#include "MessageWithTitle.h"

class MessageWithIcon : public MessageWithTitle
{
public:

	MessageWithIcon(operation op, HWND hwnd, wstring windowName, BYTE& icona, u_long iconLength);
	~MessageWithIcon();

	BYTE& serialize(u_long& size);

	BYTE& toJson(u_long& size);

protected:	
	BYTE* pixels;
	u_long iconLength;
	
};

