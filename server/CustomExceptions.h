#pragma once

#include <exception>

using namespace std;

class CtrlCHandlingException : public runtime_error
{
private:
	int error;

public:
	CtrlCHandlingException(string msg) : runtime_error(msg), error(0) {}
	CtrlCHandlingException(string msg, int error) : runtime_error(msg), error(error) {}

	int getError(void) { return error; }

};
