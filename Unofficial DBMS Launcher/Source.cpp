#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tchar.h>
#include <vector>
#include <string>
#include <fstream>
#include <atlbase.h>
#include <combaseapi.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

// Tray and menu IDs
#define WM_TRAYICON               (WM_USER + 1)
#define ID_TRAY_OPEN_SQLDEV       1001
#define ID_TRAY_OPEN_SERVER       1002
#define ID_TRAY_STOP_SERVICES     1004

const wchar_t g_szClassName[] = L"ServiceTrayAppClass";
const wchar_t g_szNoticeClass[] = L"ServiceTrayNoticeClass";
const wchar_t* SERVICE_NAMES[] = {
    L"OracleOraDB23Home1TNSListener",
    L"OracleServiceFREE",
    L"OracleVssWriterFREE"
};
std::wstring SERVER_PATH;

// License text
const wchar_t* LICENSE_TEXT =
L"Unofficial Oracle server management software. Not affiliated with Oracle Corporation.\r\n"
L"Copyright 2025 FirstName LastName\r\n\r\n"
L"Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the \"Software\"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:\r\n\r\n"
L"The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.\r\n\r\n"
L"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.";

// Helpers to manage license acknowledgement
std::wstring GetNoticePath() {
    wchar_t appdata[MAX_PATH];
    SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, appdata);
    std::wstring dir = std::wstring(appdata) + L"\\Unofficial Oracle Server Launcher";
    CreateDirectory(dir.c_str(), NULL);
    return dir + L"\\notice.cfg";
}

bool NoticeExists() {
    DWORD attr = GetFileAttributes(GetNoticePath().c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

void WriteNoticeAck() {
    std::wofstream ofs(GetNoticePath());
    ofs << L"agreed";
}

// Show license window
LRESULT CALLBACK NoticeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hEdit, hAgree, hAgreeOnce, hDecline;
    switch (msg) {
    case WM_CREATE:
        // Adjust edit control size
        hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", LICENSE_TEXT,
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            10, 10, 780, 300, hWnd, NULL, NULL, NULL);
        {
            // Calculate button positions to center them in 820px width
            int totalButtonsWidth = 120 + 20 + 200 + 20 + 120; // = 480
            int startX = (820 - totalButtonsWidth) / 2; // = 170
            int y = 320;
            hAgree = CreateWindow(L"BUTTON", L"Agree", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                startX, y, 120, 30, hWnd, (HMENU)1, NULL, NULL);
            hAgreeOnce = CreateWindow(L"BUTTON", L"Agree && Don't Show Again", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                startX + 120 + 20, y, 200, 30, hWnd, (HMENU)2, NULL, NULL);
            hDecline = CreateWindow(L"BUTTON", L"Decline", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                startX + 120 + 20 + 200 + 20, y, 120, 30, hWnd, (HMENU)3, NULL, NULL);
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 1:
            DestroyWindow(hWnd);
            break;
        case 2:
            WriteNoticeAck();
            DestroyWindow(hWnd);
            break;
        case 3:
            ExitProcess(0);
            break;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

void ShowNotice(HINSTANCE hInst) {
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = NoticeWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = g_szNoticeClass;
    RegisterClassEx(&wc);

    // Respect 820x400 resolution
    HWND hWnd = CreateWindowEx(0, g_szNoticeClass, L"License Agreement",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 820, 400,
        NULL, NULL, hInst, NULL);
    ShowWindow(hWnd, SW_SHOW);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}


// Service control helpers
bool StartNamedService(const wchar_t* serviceName) {
    SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenService(scm, serviceName, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!svc) { CloseServiceHandle(scm); return false; }
    bool ok = StartService(svc, 0, NULL) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

bool StopNamedService(const wchar_t* serviceName) {
    SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenService(scm, serviceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!svc) { CloseServiceHandle(scm); return false; }
    SERVICE_STATUS ss;
    bool ok = ControlService(svc, SERVICE_CONTROL_STOP, &ss) || GetLastError() == ERROR_SERVICE_NOT_ACTIVE;
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

// Context menu
void ShowContextMenu(HWND hWnd) {
    POINT pt; GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    InsertMenu(hMenu, -1, MF_BYPOSITION, ID_TRAY_OPEN_SQLDEV, L"Open SQL Developer");
    InsertMenu(hMenu, -1, MF_BYPOSITION, ID_TRAY_OPEN_SERVER, L"Open Server Path in File Explorer");
    InsertMenu(hMenu, -1, MF_BYPOSITION, ID_TRAY_STOP_SERVICES, L"Stop Services");
    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

// Tray icon management
void AddTrayIcon(HWND hWnd) {
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Unofficial Oracle Service Tray");
    Shell_NotifyIcon(NIM_ADD, &nid);
}

void RemoveTrayIcon(HWND hWnd) {
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = 1;
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

// Main window proc
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) ShowContextMenu(hWnd);
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_OPEN_SQLDEV:
            ShellExecute(NULL, L"open",
                L"C:\\Program Files\\sqldeveloper\\sqldeveloper.exe",
                NULL, NULL, SW_SHOWNORMAL);
            break;
        case ID_TRAY_OPEN_SERVER:
            ShellExecute(NULL, L"open",
                L"explorer.exe",
                SERVER_PATH.c_str(),
                NULL, SW_SHOWNORMAL);
            break;
        case ID_TRAY_STOP_SERVICES:
            for (auto& name : SERVICE_NAMES) StopNamedService(name);
            PostMessage(hWnd, WM_CLOSE, 0, 0);
            break;
        }
        break;
    case WM_CREATE:
        AddTrayIcon(hWnd);
        break;
    case WM_DESTROY:
        RemoveTrayIcon(hWnd);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    // Single-instance check
    HANDLE hMutex = CreateMutex(NULL, FALSE, L"Global\\UnofficialOracleServerLauncher23AI");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBox(NULL, L"It's already running. Check the system tray.", L"Info", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // Construct server path dynamically
    wchar_t user[MAX_PATH];
    DWORD len = GetEnvironmentVariable(L"USERNAME", user, MAX_PATH);
    SERVER_PATH = std::wstring(L"C:\\app\\") + std::wstring(user, len) + L"\\product\\23ai\\dbhomeFree";

    // License notice
    if (!NoticeExists()) ShowNotice(hInstance);

    // Start services
    for (auto& name : SERVICE_NAMES) StartNamedService(name);

    // Register and create invisible window
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = g_szClassName;
    RegisterClassEx(&wc);

    HWND hWnd = CreateWindowEx(0, g_szClassName, L"", 0,
        0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
