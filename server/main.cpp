#include "WindowsNotificationService.h"
#include "CustomExceptions.h"
#include <exception>
#include <iostream>

int main(int argc, char* argv[])
{
	try {
		WindowsNotificationService wns;
		wns.start();
	}
	catch (CtrlCHandlingException) {
		return -1;
	}
		
	return 0;
}

