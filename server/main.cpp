#include "WindowsNotificationService.h"
#include <exception>

int main(int argc, char* argv[])
{
	WindowsNotificationService wns;

	try {
		wns.start();
	}
	catch (exception& ex) {

	}
	
	Sleep(10000);

	return 0;
}

