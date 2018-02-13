#include <Windows.h>
#include <vector>
#include <future>
#include <map>

enum operation;

//#pragma once
using namespace std;

class Server
{

public:

	Server();
	~Server();
	void start();

private:
	SOCKET clientSocket;
	string listeningPort;

	exception_ptr globalExceptionPtr;

	promise<bool> stopNotificationsThread;
	vector<HWND> currentProgs;

	SOCKET acceptConnection();
	DWORD WINAPI notificationsManagement();
	static BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam);

	void sendApplicationToClient(SOCKET* clientSocket, HWND hwnd, operation op);	
	long ottieniIcona(BYTE* lpPixels, HWND hwnd);
	void receiveCommands();
	void sendKeystrokesToProgram(vector<UINT> vKeysList);
	
	static BOOL IsAltTabWindow(HWND hwnd);
	string getTitleFromHwnd(HWND hwnd);
	void BitmapInfoErrorExit(LPTSTR lpszFunction);
	HICON getHICONfromHWND(HWND hwnd);
	HBITMAP getHBITMAPfromHICON(HICON hIcon);
	PBITMAPINFO CreateBitmapInfoStruct(HBITMAP hBmp);

};

