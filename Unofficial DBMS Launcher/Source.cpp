#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tchar.h>
#include <vector>
#include <string>
#include <fstream>
#include <winsvc.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

// tray/menu IDs
#define WM_TRAYICON             (WM_USER + 1)
#define ID_TRAY_OPEN_SQLDEV     1001
#define ID_TRAY_OPEN_SERVER     1002
#define ID_TRAY_STOP_SERVICES   1004

const wchar_t g_szClassName[] = L"ServiceTrayAppClass";
const wchar_t g_szNoticeClass[] = L"ServiceTrayNoticeClass";

// Oracle services we manage
const wchar_t* SERVICE_NAMES[] = {
    L"OracleOraDB23Home1TNSListener",
    L"OracleServiceFREE",
    L"OracleVssWriterFREE"
};

std::wstring SERVER_PATH;

// — License‐notice machinery (unchanged) —
const wchar_t* LICENSE_TEXT =
L"Unofficial Oracle server management software. Not affiliated with Oracle Corporation.\r\n"
L"https://github.com/danielharton/Unofficial-DBMS-Launcher\r\n"
L"Copyright 2025 Daniel Harton\r\n\r\n"
L"Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the \"Software\"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:\r\n\r\n"
L"The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.\r\n\r\n"
L"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.";


std::wstring GetNoticePath() {
    wchar_t appdata[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata);
    std::wstring dir = appdata;
    dir += L"\\Unofficial Oracle Server Launcher";
    CreateDirectoryW(dir.c_str(), NULL);
    return dir + L"\\notice.cfg";
}

bool NoticeExists() {
    DWORD attr = GetFileAttributesW(GetNoticePath().c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

void WriteNoticeAck() {
    std::wofstream ofs(GetNoticePath());
    ofs << L"agreed";
}

LRESULT CALLBACK NoticeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hEdit, hAgree, hAgreeOnce, hDecline;
    switch (msg) {
    case WM_CREATE:
        hEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", LICENSE_TEXT,
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            10, 10, 780, 300, hWnd, nullptr, nullptr, nullptr);

        {
            int totalW = 120 + 20 + 200 + 20 + 120;
            int startX = (820 - totalW) / 2, y = 320;
            hAgree = CreateWindowW(L"BUTTON", L"Agree",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                startX, y, 120, 30, hWnd, (HMENU)1, nullptr, nullptr);
            hAgreeOnce = CreateWindowW(L"BUTTON", L"Agree && Don't Show Again",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                startX + 140, y, 200, 30, hWnd, (HMENU)2, nullptr, nullptr);
            hDecline = CreateWindowW(L"BUTTON", L"Decline",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                startX + 360, y, 120, 30, hWnd, (HMENU)3, nullptr, nullptr);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 1: DestroyWindow(hWnd);          break;
        case 2: WriteNoticeAck(); DestroyWindow(hWnd); break;
        case 3: ExitProcess(0);               break;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

void ShowNotice(HINSTANCE hInst) {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = NoticeWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = g_szNoticeClass;
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        0, g_szNoticeClass, L"License Agreement",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 820, 400,
        NULL, NULL, hInst, NULL);
    ShowWindow(hWnd, SW_SHOW);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// — Service helpers — 
bool StartNamedService(const wchar_t* name) {
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!svc) { CloseServiceHandle(scm); return false; }
    bool ok = StartServiceW(svc, 0, nullptr)
        || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
}

bool StopNamedService(const wchar_t* name) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!svc) { CloseServiceHandle(scm); return false; }

    SERVICE_STATUS_PROCESS ssp = {};
    DWORD bytesNeeded = 0;

    // 1) See where we are right now
    if (!QueryServiceStatusEx(
        svc,
        SC_STATUS_PROCESS_INFO,
        (LPBYTE)&ssp,
        sizeof(ssp),
        &bytesNeeded))
    {
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return false;
    }

    // If already stopped, we’re done
    if (ssp.dwCurrentState == SERVICE_STOPPED) {
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return true;
    }

    // 2) Send STOP control (works even if we’re in START_PENDING)
    ControlService(svc, SERVICE_CONTROL_STOP, (LPSERVICE_STATUS)&ssp);

    // 3) Poll every 200 ms up to 10 s
    /*const DWORD kTimeoutMs = 10'000;
    DWORD waited = 0;*/
    /*while (ssp.dwCurrentState != SERVICE_STOPPED && waited < kTimeoutMs) {
        Sleep(200);
        waited += 200;
        if (!QueryServiceStatusEx(
            svc,
            SC_STATUS_PROCESS_INFO,
            (LPBYTE)&ssp,
            sizeof(ssp),
            &bytesNeeded))
        {
            break;
        }
    }*/

    bool stopped = (ssp.dwCurrentState == SERVICE_STOPPED);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return stopped;
}

// — Tray & menu — 
void AddTrayIcon(HWND hWnd) {
    NOTIFYICONDATAW nid = { sizeof(nid) };
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Unofficial Oracle Service Tray");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void RemoveTrayIcon(HWND hWnd) {
    NOTIFYICONDATAW nid = { sizeof(nid) };
    nid.hWnd = hWnd; nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowContextMenu(HWND hWnd) {
    POINT pt; GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING,
        ID_TRAY_OPEN_SQLDEV, L"Open SQL Developer");
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING,
        ID_TRAY_OPEN_SERVER, L"Open Server Path...");
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING,
        ID_TRAY_STOP_SERVICES, L"Stop Services");
    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static bool stopping = false;
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP && !stopping)
            ShowContextMenu(hWnd);
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_OPEN_SQLDEV:
            ShellExecuteW(NULL, L"open",
                L"C:\\Program Files\\sqldeveloper\\sqldeveloper.exe",
                NULL, NULL, SW_SHOWNORMAL);
            break;

        case ID_TRAY_OPEN_SERVER:
            ShellExecuteW(NULL, L"open",
                L"explorer.exe",
                SERVER_PATH.c_str(),
                NULL, SW_SHOWNORMAL);
            break;

        case ID_TRAY_STOP_SERVICES:
            stopping = true;
            // balloon tip
            {
                NOTIFYICONDATAW tip = { sizeof(tip) };
                tip.hWnd = hWnd; tip.uID = 1;
                tip.uFlags = NIF_INFO;
                wcscpy_s(tip.szInfoTitle, L"Service Tray");
                wcscpy_s(tip.szInfo, L"Stopping Oracle services...\nPlease wait.");
                tip.dwInfoFlags = NIIF_INFO;
                Shell_NotifyIconW(NIM_MODIFY, &tip);
            }
            // retry each service until stopped
            for (auto& svc : SERVICE_NAMES) {
                while (!StopNamedService(svc)) {
                    Sleep(1000);
                }
            }
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
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
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_     PWSTR    lpCmdLine,
    _In_     int      nCmdShow
) {
    // single-instance
    HANDLE mtx = CreateMutexW(NULL, FALSE,
        L"Global\\UnofficialOracleServerLauncher23AI");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL,
            L"It's already running. Check the system tray.",
            L"Info", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // dynamic server path
    wchar_t user[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(
        L"USERNAME", user, MAX_PATH);
    SERVER_PATH = L"C:\\app\\"
        + std::wstring(user, len)
        + L"\\product\\23ai\\dbhomeFree";

    // show license once
    if (!NoticeExists())
        ShowNotice(hInstance);

    // start services on launch
    for (auto& svc : SERVICE_NAMES)
        StartNamedService(svc);

    // create hidden tray window
    WNDCLASSEXW wcx = { sizeof(wcx) };
    wcx.lpfnWndProc = WndProc;
    wcx.hInstance = hInstance;
    wcx.lpszClassName = g_szClassName;
    RegisterClassExW(&wcx);

    HWND hWnd = CreateWindowExW(
        0, g_szClassName, L"", 0, 0, 0, 0, 0,
        NULL, NULL, hInstance, NULL);
    ShowWindow(hWnd, SW_HIDE);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
