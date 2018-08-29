
// EchoCon.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <Windows.h>
#include <conio.h>
#include <process.h>    /* _beginthread, _endthread */
#include <wchar.h>

// Initializes the specified startup info struct with the required properties and
// updates its thread attribute list with the specified ConPTY handle
HRESULT InitializeStartupInfoAttachedToConPTY(STARTUPINFOEX* pStartupInfo, HPCON hPC)
{
    HRESULT hr = E_UNEXPECTED;

    if (pStartupInfo)
    {
        SIZE_T size = 0;

        pStartupInfo->StartupInfo.cb = sizeof(STARTUPINFOEX);

        // Get the size of the thread attribute list.
        InitializeProcThreadAttributeList(NULL, 1, 0, &size);

        // Allocate a thread attribute list of the correct size
        pStartupInfo->lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc((size_t)size);

        // Initialize thread attribute list
        if (pStartupInfo->lpAttributeList &&
            InitializeProcThreadAttributeList(pStartupInfo->lpAttributeList, 1, 0, &size))
        {
            // Set thread attribute list's Pseudo Console to the specified ConPTY
            hr = UpdateProcThreadAttribute(
                pStartupInfo->lpAttributeList,
                0,
                PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                hPC,
                sizeof(HPCON),
                NULL,
                NULL)
                ? S_OK
                : HRESULT_FROM_WIN32(GetLastError());
        }
        else
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
        }
    }
    return hr;
}

COORD GetConsoleSize(HANDLE hStdOut)
{
    COORD size = { 0, 0 };

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
    {
        size.X = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        size.Y = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }

    return size;
}

void __cdecl InputThreadFunction(void* pArg)
{
    wchar_t ch = L'A';

    while (L' ' != ch)
    {
        ch = _getwch();
    }

    _tprintf(L"\r\n--> Input thread terminating\r\n");

    // Closes the thread handle so no need for later clean-up.
    _endthread();
}

void __cdecl OutputThreadFunction(void* pArg)
{
    HANDLE hPipe = (HANDLE)pArg;

    const DWORD BUFF_SIZE = 512 * sizeof(char);
    char* pszBuffer = (char*)malloc(BUFF_SIZE);
    memset(pszBuffer, 0, BUFF_SIZE);
    DWORD dwBytesRead = 0;

    while (true)
    {
        if (ReadFile(hPipe, pszBuffer, BUFF_SIZE, &dwBytesRead, NULL)
            && dwBytesRead > 0)
        {
            printf(pszBuffer);
            _flushall();
        }
        else
        {
            // Update the Console once every second if nothing received last time
            Sleep(1000);
        }
    }

    _tprintf(L"\r\nOutput thread terminating\r\n");

    free((void*)pszBuffer);

    // Closes the thread handle so no need for later clean-up.
    _endthread();
}

int main()
{
    HPCON hPC = 0;
    HRESULT hr = 0;
    HANDLE hPipePTYIn = 0;
    HANDLE hPipePTYOut = 0;
    HANDLE hPipeIn = 0;
    HANDLE hPipeOut = 0;
    HANDLE hInputThread = 0;
    HANDLE hOutputThread = 0;
    PROCESS_INFORMATION piClient{};

    STARTUPINFOEX startupInfo{};

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD consoleMode;
    GetConsoleMode(hConsole, &consoleMode);
    SetConsoleMode(hConsole, consoleMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_VIRTUAL_TERMINAL_INPUT);
    COORD consoleSize = GetConsoleSize(hConsole);

    // Create the pipes to which the ConPTY will connect
    if (CreatePipe(&hPipePTYIn, &hPipeOut, NULL, 0) &&
        CreatePipe(&hPipeIn, &hPipePTYOut, NULL, 0))
    {
        // Create ConPTY
        hr = CreatePseudoConsole(consoleSize, hPipePTYIn, hPipePTYOut, 0, &hPC);

        // Initialize the necessary startup info struct        
        if (S_OK == InitializeStartupInfoAttachedToConPTY(&startupInfo, hPC))
        {
            // Create threads to listen for user-input and app-output
            if ((hInputThread = (HANDLE)_beginthread(&InputThreadFunction, 0, NULL))
                && (hOutputThread = (HANDLE)_beginthread(&OutputThreadFunction, 0, (void*)hPipeIn)))
            {
                // Launch ping to echo some text back
                TCHAR szCommand[] = L"ping 8.8.8.8";
                _tprintf_s(L"\033[32;1m Executing Command: `%s` \033[0m\n", szCommand);
                BOOL fSuccess = CreateProcess(
                    nullptr,
                    szCommand,
                    nullptr,
                    nullptr,
                    TRUE,
                    EXTENDED_STARTUPINFO_PRESENT,
                    nullptr,
                    nullptr,
                    &startupInfo.StartupInfo,
                    &piClient);

                // Wait until the input thread returns
                WaitForSingleObject(hInputThread, INFINITE);
            }
            // Close ConPTY - this will terminate client process if running
            ClosePseudoConsole(hPC);

            // Now safe to clean-up client process info process & thread
            CloseHandle(piClient.hProcess);
            CloseHandle(piClient.hThread);

            // Cleanup attribute list
            if (startupInfo.lpAttributeList)
            {
                DeleteProcThreadAttributeList(startupInfo.lpAttributeList);
                free(startupInfo.lpAttributeList);
            }
        }
        else
        {
            _tprintf_s(L"Error: Failed to initialize StartupInfo attached to ConPTY [0x%x]", GetLastError());
        }
    }
    else
    {
        _tprintf_s(L"Error: Failed to create pipes");
    }

    // Clean-up the pipes
    if (hPipeIn) CloseHandle(hPipePTYIn);
    if (hPipePTYOut) CloseHandle(hPipePTYOut);
    if (hPipeOut) CloseHandle(hPipeOut);
    if (hPipeIn) CloseHandle(hPipeIn);

    // Restore console to its original mode.
    SetConsoleMode(hConsole, consoleMode);

    return hr;
}

