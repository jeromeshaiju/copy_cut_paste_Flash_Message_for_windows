#include <windows.h>
#include <string>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "dwmapi.lib")

#include <dwmapi.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif

HHOOK keyboardHook;
HWND notifWnd;
HFONT hFontLabel;
HFONT hFontContent;

std::wstring gLabel;
std::wstring gContent;
bool gEnabled = true;
#define WM_APP_TRAYMSG (WM_APP + 1)

std::wstring GetClipboardText() {
    if (!OpenClipboard(NULL)) return L"";
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) { CloseClipboard(); return L""; }
    wchar_t* pText = (wchar_t*)GlobalLock(hData);
    std::wstring result = pText ? pText : L"";
    GlobalUnlock(hData);
    CloseClipboard();
    // remove newlines
    for (auto& c : result) if (c == L'\n' || c == L'\r') c = L' ';
    return result;
}

std::wstring TrimText(const std::wstring& text, int maxChars = 40) {
    if ((int)text.size() <= maxChars) return text;
    int half = (maxChars - 3) / 2;
    return text.substr(0, half) + L"........" + text.substr(text.size() - half);
}

LRESULT CALLBACK NotifWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        SetBkMode(hdc, TRANSPARENT);

        // Determine colors based on action
        COLORREF bgColor = RGB(40, 40, 40); // fallback
        COLORREF textColor = RGB(220, 220, 220);
        COLORREF labelColor = RGB(255, 255, 255);
        
        if (gLabel == L"COPIED") {
            bgColor = RGB(145, 198, 188); // 9AD872
            textColor = RGB(40, 40, 40);
            labelColor = RGB(20, 20, 20);
        } else if (gLabel == L"PASTED") {
            bgColor = RGB(0xFF, 0xEF, 0x91); // FFEF91
            textColor = RGB(50, 50, 50);
            labelColor = RGB(20, 20, 20);
        } else if (gLabel == L"CUT") {
            bgColor = RGB(75, 157, 169); // 468432
            textColor = RGB(240, 240, 240);
            labelColor = RGB(255, 255, 255);
        }

        // Fill background
        HBRUSH bg = CreateSolidBrush(bgColor);
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        // Fonts and UI layout
        int marginX = 14;
        SelectObject(hdc, hFontLabel);
        SetTextColor(hdc, labelColor);

        if (gContent.empty()) {
            // center vertically if no content
            RECT labelRect = { marginX, 0, rc.right - marginX, rc.bottom };
            DrawTextW(hdc, gLabel.c_str(), -1, &labelRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        } else {
            RECT labelRect = { marginX, 4, rc.right - marginX, 24 };
            DrawTextW(hdc, gLabel.c_str(), -1, &labelRect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            // Content text
            SelectObject(hdc, hFontContent);
            SetTextColor(hdc, textColor);
            RECT contentRect = { marginX, 24, rc.right - marginX, rc.bottom - 4 };
            DrawTextW(hdc, gContent.c_str(), -1, &contentRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        ShowWindow(hwnd, SW_HIDE);
        KillTimer(hwnd, 1);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

void ShowNotif(const std::wstring& label, const std::wstring& content = L"") {
    gLabel = label;
    gContent = content.empty() ? L"" : TrimText(content);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int w = content.empty() ? 120 : 280;
    int h = content.empty() ? 32 : 46;
    int x = (sw - w) / 2;
    int y = 24;

    SetWindowPos(notifWnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    
    // We no longer use SetWindowRgn! The DWM will handle the rounded corners perfectly Native on Win11+.

    InvalidateRect(notifWnd, NULL, FALSE);
    SetTimer(notifWnd, 1, 1500, NULL);
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wp, LPARAM lp) {
    if (!gEnabled) return CallNextHookEx(keyboardHook, nCode, wp, lp);

    if (nCode == HC_ACTION && wp == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lp;
        bool ctrl = GetAsyncKeyState(VK_CONTROL) & 0x8000;

        // Built-in shortcut to cleanly and permanently close the background app: Ctrl + Shift + Q
        bool shift = GetAsyncKeyState(VK_SHIFT) & 0x8000;
        if (ctrl && shift && kb->vkCode == 'Q') {
            PostQuitMessage(0);
            return 1;
        }

        // Ignore hotkeys while the notification is currently visible on screen
        if (ctrl && !IsWindowVisible(notifWnd)) {
            static ULONGLONG lastEventTime = 0;
            static DWORD lastVkCode = 0;
            ULONGLONG now = GetTickCount64();

            if (kb->vkCode == 'X' || kb->vkCode == 'C' || kb->vkCode == 'V') {
                if (kb->vkCode != lastVkCode || now - lastEventTime > 500) {
                    lastVkCode = kb->vkCode;
                    lastEventTime = now;

                    if (kb->vkCode == 'X') {
                        SetTimer(notifWnd, 3, 100, NULL);
                    }
                    else if (kb->vkCode == 'C') {
                        SetTimer(notifWnd, 2, 100, NULL);
                    }
                    else if (kb->vkCode == 'V') {
                        ShowNotif(L"PASTED");
                    }
                }
            }
        }
    }
    return CallNextHookEx(keyboardHook, nCode, wp, lp);
}

LRESULT CALLBACK NotifWndProcWrapper(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_APP_TRAYMSG) {
        if (lp == WM_RBUTTONUP || lp == WM_LBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, 1001, gEnabled ? L"Pause Notifications" : L"Resume Notifications");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, 1002, L"Exit Application");
            
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
            
            if (cmd == 1001) gEnabled = !gEnabled;
            else if (cmd == 1002) PostQuitMessage(0);
        }
        return 0;
    }
    if (msg == WM_TIMER && wp == 2) {
        KillTimer(hwnd, 2);
        std::wstring clipText = GetClipboardText();
        ShowNotif(L"COPIED", clipText);
        return 0;
    }
    if (msg == WM_TIMER && wp == 3) {
        KillTimer(hwnd, 3);
        std::wstring clipText = GetClipboardText();
        ShowNotif(L"CUT", clipText);
        return 0;
    }
    return NotifWndProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    SetProcessDPIAware(); // Completely eliminates OS bitmap scaling "pixelation" on high-DPI displays!

    WNDCLASSW wc = {};
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = NotifWndProcWrapper;
    wc.hInstance = hInst;
    wc.lpszClassName = L"NotifClass";
    RegisterClassW(&wc);

    hFontLabel = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    hFontContent = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    // Removed WS_EX_LAYERED and LWA_ALPHA to enable pristine ClearType font rendering!
    notifWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"NotifClass", L"", WS_POPUP,
        0, 0, 340, 54, NULL, NULL, hInst, NULL
    );

    // Let the Windows compositor draw perfect anti-aliased rounded corners without any RAM overhead!
    DWORD preference = DWMWCP_ROUND;
    DwmSetWindowAttribute(notifWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
    
    // Remove the native 1px frame border for a flush, pristine look
    DWORD borderColor = DWMWA_COLOR_NONE;
    DwmSetWindowAttribute(notifWnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = notifWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAYMSG;

    // Load custom icon if available, otherwise use default
    HICON hCustomIcon = (HICON)LoadImageW(NULL, L"icon.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    nid.hIcon = hCustomIcon ? hCustomIcon : LoadIcon(NULL, IDI_INFORMATION);

    lstrcpyW(nid.szTip, L"Copy/Cut Notifier");
    Shell_NotifyIconW(NIM_ADD, &nid);

    keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, NULL, 0);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &nid);
    UnhookWindowsHookEx(keyboardHook);
    return 0;
}