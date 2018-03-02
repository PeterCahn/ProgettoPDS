#include "MessageWithTitle.h"

class MessageWithIcon : public MessageWithTitle
{
public:
	// OPENP
	MessageWithIcon(operation op, HWND hwnd, wstring windowName, BYTE& icona, u_long iconLength);
	~MessageWithIcon();

	BYTE& serialize(u_long& size);

//private:	
	BYTE* pixels;
	u_long iconLength;
	
};

