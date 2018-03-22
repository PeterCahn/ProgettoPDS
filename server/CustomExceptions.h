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


class InternalServerStartError : public runtime_error
{
private:
	int error;

public:
	InternalServerStartError(string msg) : runtime_error(msg), error(0) {}
	InternalServerStartError(string msg, int error) : runtime_error(msg), error(error) {}

	int getError(void) { return error; }

};


class ReadPortNumberException : public runtime_error
{
private:
	int error;

public:
	ReadPortNumberException(string msg) : runtime_error(msg), error(0) {}
	ReadPortNumberException(string msg, int error) : runtime_error(msg), error(error) {}

	int getError(void) { return error; }

};

class SendMessageException : public runtime_error
{
private:
	int error;

public:
	SendMessageException(string msg) : runtime_error(msg), error(0) {}
	SendMessageException(string msg, int error) : runtime_error(msg), error(error) {}

	int getError(void) { return error; }

};


class MessageCreationException : public runtime_error
{
private:
	int error;

public:
	MessageCreationException(string msg) : runtime_error(msg), error(0) {}
	MessageCreationException(string msg, int error) : runtime_error(msg), error(error) {}

	int getError(void) { return error; }

};