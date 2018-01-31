#include <Windows.h>
enum operation;

#pragma once

class Server
{

private:
	SOCKET clientSocket;

public:

	Server();
	~Server();
	void start();

};

