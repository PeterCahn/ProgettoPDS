#include "WindowsNotificationService.h"
#include <exception>
#include <iostream>

int main(int argc, char* argv[])
{
	WindowsNotificationService wns;

	try {
		wns.start();
	}
	catch (exception& ex) {
		//wcout << ex.what() << endl;
		wns.stop();
	}
		
	//system("pause");

	return 0;
}

