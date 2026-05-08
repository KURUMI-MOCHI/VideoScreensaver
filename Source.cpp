#pragma comment(lib, "scrnsavw")
#pragma comment(lib, "comctl32")
#pragma comment(lib, "dwmapi")
#pragma comment(lib, "shlwapi")

#include <windows.h>
#include <scrnsave.h>
#include <atlbase.h>
#include <atlwin.h>
#include <wmp.h>
#include <vector>
#include <random>
#include <shlwapi.h>
#include "resource.h"

CComModule _Module;
WNDPROC DefaultListBoxWndProc; // ← エラーの原因（宣言漏れ）を修正
HWND _hMainWindowHandle;

// --- 設定クラス ---
#define REG_KEY L"Software\\VideoScreensaver\\Setting"
class Setting {
    std::vector<LPTSTR> m_lpszFilePathList;
    DWORD m_dwMute, m_dwRandom;
public:
    Setting() : m_dwMute(TRUE), m_dwRandom(TRUE) {}
    ~Setting() { ClearFilePath(); }
    void Load() {
        HKEY hKey;
        if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &hKey)) {
            DWORD dwType, dwByte, nCount = 0; dwByte = sizeof(DWORD);
            RegQueryValueEx(hKey, L"FilePathCount", NULL, &dwType, (BYTE*)&nCount, &dwByte);
            for (DWORD i = 0; i < nCount; ++i) {
                WCHAR szKey[16]; wsprintf(szKey, L"FilePath%d", i);
                if (ERROR_SUCCESS == RegQueryValueEx(hKey, szKey, NULL, &dwType, NULL, &dwByte)) {
                    LPTSTR path = (LPTSTR)GlobalAlloc(GPTR, dwByte);
                    RegQueryValueEx(hKey, szKey, NULL, &dwType, (BYTE*)path, &dwByte);
                    m_lpszFilePathList.push_back(path);
                }
            }
            dwByte = sizeof(DWORD); RegQueryValueEx(hKey, L"Mute", NULL, &dwType, (BYTE*)&m_dwMute, &dwByte);
            dwByte = sizeof(DWORD); RegQueryValueEx(hKey, L"Random", NULL, &dwType, (BYTE*)&m_dwRandom, &dwByte);
            RegCloseKey(hKey);
        }
    }
    void Save() {
        HKEY hKey;
        if (ERROR_SUCCESS == RegCreateKeyEx(HKEY_CURRENT_USER, REG_KEY, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL)) {
            DWORD nCount = (DWORD)m_lpszFilePathList.size();
            RegSetValueEx(hKey, L"FilePathCount", 0, REG_DWORD, (BYTE*)&nCount, sizeof(DWORD));
            for (DWORD i = 0; i < nCount; ++i) {
                WCHAR szKey[16]; wsprintf(szKey, L"FilePath%d", i);
                RegSetValueEx(hKey, szKey, 0, REG_SZ, (BYTE*)m_lpszFilePathList[i], (lstrlen(m_lpszFilePathList[i]) + 1) * sizeof(WCHAR));
            }
            RegSetValueEx(hKey, L"Mute", 0, REG_DWORD, (BYTE*)&m_dwMute, sizeof(DWORD));
            RegSetValueEx(hKey, L"Random", 0, REG_DWORD, (BYTE*)&m_dwRandom, sizeof(DWORD));
            RegCloseKey(hKey);
        }
    }
    int GetFilePathCount() { return (int)m_lpszFilePathList.size(); }
    LPCTSTR GetFilePath(int i) { return m_lpszFilePathList[i]; }
    BOOL GetMute() { return m_dwMute; }
    BOOL GetRandom() { return m_dwRandom; }
    void ClearFilePath() { for (auto p : m_lpszFilePathList) GlobalFree(p); m_lpszFilePathList.clear(); }
    void AddFilePath(LPCTSTR p) { LPTSTR s = (LPTSTR)GlobalAlloc(GPTR, (lstrlen(p) + 1) * sizeof(WCHAR)); lstrcpy(s, p); m_lpszFilePathList.push_back(s); }
    void SetMute(BOOL b) { m_dwMute = b; }
    void SetRandom(BOOL b) { m_dwRandom = b; }
};

// --- スクリーンセーバー本体 ---
LRESULT WINAPI ScreenSaverProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static Setting setting;
    static HWND hWMP = NULL;
    static BOOL bPreview;

    switch (msg) {
    case WM_CREATE:
        setting.Load();
        bPreview = ((LPCREATESTRUCT)lParam)->style & WS_CHILD;
        AtlAxWinInit();
        hWMP = CreateWindow(L"AtlAxWin", L"WMPlayer.OCX.7", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, hWnd, NULL, NULL, NULL);
        SetTimer(hWnd, 100, 150, NULL); // WMPの準備を待つ
        break;

    case WM_TIMER:
        if (wParam == 100) {
            KillTimer(hWnd, 100);
            if (!hWMP || setting.GetFilePathCount() == 0) return 0;
            CComPtr<IUnknown> spUnk;
            if (SUCCEEDED(AtlAxGetControl(hWMP, &spUnk))) {
                CComPtr<IWMPPlayer> spPlayer;
                spUnk->QueryInterface(__uuidof(IWMPPlayer), (void**)&spPlayer);
                if (spPlayer) {
                    RECT rc; GetWindowRect(hWnd, &rc);
                    CComPtr<IWMPSettings> spSet;
                    spPlayer->get_settings(&spSet);
                    if (spSet) {
                        BOOL bMute = (rc.left == 0 && rc.top == 0 && !bPreview && !setting.GetMute()) ? FALSE : TRUE;
                        spSet->put_mute(bMute ? VARIANT_TRUE : VARIANT_FALSE);
                    }
                    int idx = (setting.GetRandom() && setting.GetFilePathCount() > 1) ? (rand() % setting.GetFilePathCount()) : 0;
                    spPlayer->put_URL(CComBSTR(setting.GetFilePath(idx)));
                }
            }
        }
        break;

    case WM_SIZE:
        if (hWMP) {
            RECT rc; GetClientRect(hWnd, &rc);
            MoveWindow(hWMP, 0, 0, rc.right, rc.bottom, TRUE);
        }
        break;

    case WM_DESTROY:
        AtlAxWinTerm();
        PostQuitMessage(0);
        break;
    }
    return DefScreenSaverProc(hWnd, msg, wParam, lParam);
}

// --- 設定画面用プロシージャ ---
LRESULT CALLBACK MyListBoxProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_DELETE) PostMessage(GetParent(hWnd), WM_COMMAND, IDC_BUTTON_DELETE, 0);
        break;
    }
    return CallWindowProc(DefaultListBoxWndProc, hWnd, msg, wParam, lParam);
}

BOOL WINAPI ScreenSaverConfigureDialog(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static Setting setting;
    switch (msg) {
    case WM_INITDIALOG:
        setting.Load();
        for (int i = 0; i < setting.GetFilePathCount(); ++i) {
            SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_ADDSTRING, 0, (LPARAM)setting.GetFilePath(i));
        }
        SendDlgItemMessage(hWnd, IDC_CHECK_MUTE, BM_SETCHECK, setting.GetMute() ? BST_CHECKED : BST_UNCHECKED, 0);
        SendDlgItemMessage(hWnd, IDC_CHECK_RANDOM, BM_SETCHECK, setting.GetRandom() ? BST_CHECKED : BST_UNCHECKED, 0);
        DefaultListBoxWndProc = (WNDPROC)SetWindowLongPtr(GetDlgItem(hWnd, IDC_VIDEO_LIST), GWLP_WNDPROC, (LONG_PTR)MyListBoxProc);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BUTTON_ADD:
            {
                WCHAR szFile[MAX_PATH] = {0};
                OPENFILENAME ofn = { sizeof(ofn), hWnd, NULL, L"Video Files\0*.avi;*.mp4;*.wmv;*.mpg\0", NULL, 0, 1, szFile, MAX_PATH, NULL, 0, NULL, L"Select Video", OFN_FILEMUSTEXIST };
                if (GetOpenFileName(&ofn)) {
                    SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_ADDSTRING, 0, (LPARAM)szFile);
                }
            }
            break;
        case IDC_BUTTON_DELETE:
            {
                int sel = (int)SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR) SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_DELETESTRING, sel, 0);
            }
            break;
        case IDOK:
            setting.ClearFilePath();
            for (int i = 0; i < SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_GETCOUNT, 0, 0); ++i) {
                WCHAR szPath[MAX_PATH];
                SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_GETTEXT, i, (LPARAM)szPath);
                setting.AddFilePath(szPath);
            }
            setting.SetMute(SendDlgItemMessage(hWnd, IDC_CHECK_MUTE, BM_GETCHECK, 0, 0));
            setting.SetRandom(SendDlgItemMessage(hWnd, IDC_CHECK_RANDOM, BM_GETCHECK, 0, 0));
            setting.Save();
            EndDialog(hWnd, IDOK);
            break;
        case IDCANCEL:
            EndDialog(hWnd, IDCANCEL);
            break;
        }
        break;
    }
    return FALSE;
}

BOOL WINAPI RegisterDialogClasses(HANDLE hInst) { return TRUE; }
