#pragma comment(lib, "scrnsavw")
#pragma comment(lib, "comctl32")
#pragma comment(lib, "dwmapi")

#include <windows.h>
#include <scrnsave.h>
#include <atlbase.h>
#include <atlwin.h>
#include <wmp.h>
#include <dwmapi.h>
#include <vector>
#include <algorithm>
#include <functional>
#include <random>
#include "CWMPEventDispatch.h"
#include "resource.h"

CComModule _Module;
HWND _hMainWindowHandle;
WNDPROC DefaultVideoWndProc;
WNDPROC DefaultListBoxWndProc;

BEGIN_OBJECT_MAP(ObjectMap)
END_OBJECT_MAP()

// --- ヘルパー関数群 ---

// WMPコントロールを作成する
HWND CreateWMPControl(HWND hWndParent)
{
    AtlAxWinInit();
    HWND hControl = CreateWindow(L"AtlAxWin", L"WMPlayer.OCX.7", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0, hWndParent, NULL, NULL, NULL);
    return hControl;
}

// 指定したパスの動画を再生する
void PlayVideo(HWND hWMP, LPCTSTR lpszFilePath)
{
    if (!hWMP || !lpszFilePath) return;
    CComPtr<IUnknown> pUnknown;
    if (SUCCEEDED(AtlAxGetControl(hWMP, &pUnknown)))
    {
        CComPtr<IWMPPlayer> pIWMPPlayer;
        if (SUCCEEDED(pUnknown->QueryInterface(__uuidof(IWMPPlayer), (VOID**)&pIWMPPlayer)))
        {
            pIWMPPlayer->put_URL(CComBSTR(lpszFilePath));
            pIWMPPlayer.Release();
        }
        pUnknown.Release();
    }
}

// --- 設定管理クラス ---
#define REG_KEY L"Software\\VideoScreensaver\\Setting"
class Setting {
    std::vector<LPTSTR> m_lpszFilePathList;
    DWORD m_dwMute;
    DWORD m_dwRandom;
public:
    Setting() : m_dwMute(TRUE), m_dwRandom(TRUE) {}
    ~Setting() { ClearFilePath(); }
    void Load() {
        HKEY hKey;
        if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &hKey)) {
            DWORD dwType, dwByte, nFilePathCount = 0;
            dwByte = sizeof(DWORD);
            RegQueryValueEx(hKey, L"FilePathCount", NULL, &dwType, (BYTE*)&nFilePathCount, &dwByte);
            for (DWORD i = 0; i < nFilePathCount; ++i) {
                WCHAR szKeyName[16];
                wsprintf(szKeyName, L"FilePath%d", i);
                if (ERROR_SUCCESS == RegQueryValueEx(hKey, szKeyName, NULL, &dwType, NULL, &dwByte)) {
                    LPTSTR lpszFilePath = (LPTSTR)GlobalAlloc(0, dwByte);
                    RegQueryValueEx(hKey, szKeyName, NULL, &dwType, (BYTE*)lpszFilePath, &dwByte);
                    m_lpszFilePathList.push_back(lpszFilePath);
                }
            }
            dwByte = sizeof(DWORD);
            RegQueryValueEx(hKey, L"Mute", NULL, &dwType, (BYTE*)&m_dwMute, &dwByte);
            RegQueryValueEx(hKey, L"Random", NULL, &dwType, (BYTE*)&m_dwRandom, &dwByte);
            RegCloseKey(hKey);
        }
    }
    void Save() {
        HKEY hKey;
        if (ERROR_SUCCESS == RegCreateKeyEx(HKEY_CURRENT_USER, REG_KEY, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL)) {
            // ファイル数の保存
            DWORD nFilePathCount = (DWORD)m_lpszFilePathList.size();
            RegSetValueEx(hKey, L"FilePathCount", 0, REG_DWORD, (BYTE*)&nFilePathCount, sizeof(DWORD));
            
            // 各ファイルパスの保存
            for (DWORD i = 0; i < nFilePathCount; ++i) {
                WCHAR szKeyName[16];
                wsprintf(szKeyName, L"FilePath%d", i);
                RegSetValueEx(hKey, szKeyName, 0, REG_SZ, (BYTE*)m_lpszFilePathList[i], (lstrlen(m_lpszFilePathList[i]) + 1) * sizeof(WCHAR));
            }
            
            // ミュートとランダム設定の保存
            RegSetValueEx(hKey, L"Mute", 0, REG_DWORD, (BYTE*)&m_dwMute, sizeof(DWORD));
            RegSetValueEx(hKey, L"Random", 0, REG_DWORD, (BYTE*)&m_dwRandom, sizeof(DWORD));
            
            RegCloseKey(hKey);
        }
    }
    int GetFilePathCount() { return (int)m_lpszFilePathList.size(); }
    LPCTSTR GetFilePath(int i) { return m_lpszFilePathList[i]; }
    BOOL GetMute() { return m_dwMute != FALSE; }
    BOOL GetRandom() { return m_dwRandom != FALSE; }
    void ClearFilePath() { for (auto item : m_lpszFilePathList) GlobalFree(item); m_lpszFilePathList.clear(); }
    void AddFilePath(LPCTSTR path) {
        LPTSTR p = (LPTSTR)GlobalAlloc(0, (lstrlen(path) + 1) * sizeof(WCHAR));
        lstrcpy(p, path);
        m_lpszFilePathList.push_back(p);
    }
    void SetMute(BOOL b) { m_dwMute = b; }
    void SetRandom(BOOL b) { m_dwRandom = b; }
};

// --- スクリーンセーバー プロシージャ ---
LRESULT WINAPI ScreenSaverProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static Setting setting;
    static HWND hWindowsMediaPlayerControl = NULL;
    static BOOL bPreviewMode;

    switch (msg)
    {
    case WM_CREATE:
        {
            setting.Load();
            bPreviewMode = ((LPCREATESTRUCT)lParam)->style & WS_CHILD;
            
            // 独立再生：全画面に自分専用のWMPを作る
            hWindowsMediaPlayerControl = CreateWMPControl(hWnd);
            
            if (hWindowsMediaPlayerControl) {
                // メインモニター（座標 0,0）判定
                RECT rc;
                GetWindowRect(hWnd, &rc);
                
                CComPtr<IUnknown> pUnknown;
                if (SUCCEEDED(AtlAxGetControl(hWindowsMediaPlayerControl, &pUnknown))) {
                    CComPtr<IWMPSettings> pSettings;
                    if (SUCCEEDED(pUnknown->QueryInterface(__uuidof(IWMPSettings), (VOID**)&pSettings))) {
                        // メインかつ非プレビューかつ設定がMuteでない場合のみ音を出す
                        if (rc.left == 0 && rc.top == 0 && !bPreviewMode && !setting.GetMute()) {
                            pSettings->put_mute(VARIANT_FALSE);
                            pSettings->put_volume(50);
                        } else {
                            pSettings->put_mute(VARIANT_TRUE);
                        }
                    }
                    
                    // 初期サイズ設定
                    RECT rcClient;
                    GetClientRect(hWnd, &rcClient);
                    MoveWindow(hWindowsMediaPlayerControl, 0, 0, rcClient.right, rcClient.bottom, TRUE);

                    // 再生開始
                    if (setting.GetFilePathCount() > 0) {
                        int index = 0;
                        if (setting.GetRandom()) {
                            std::random_device rd;
                            std::mt19937 g(rd());
                            std::uniform_int_distribution<int> dist(0, setting.GetFilePathCount() - 1);
                            index = dist(g);
                        }
                        PlayVideo(hWindowsMediaPlayerControl, setting.GetFilePath(index));
                    }
                }
            }
        }
        break;

    case WM_SIZE:
        {
            RECT rect;
            GetClientRect(hWnd, &rect);
            if (hWindowsMediaPlayerControl) {
                // 自分のサイズに合わせるだけ（複雑な転送はしない）
                MoveWindow(hWindowsMediaPlayerControl, 0, 0, rect.right, rect.bottom, TRUE);
            }
        }
        break;

    case WM_DESTROY:
        if (hWindowsMediaPlayerControl) DestroyWindow(hWindowsMediaPlayerControl);
        AtlAxWinTerm();
        PostQuitMessage(0);
        break;
    }
    return DefScreenSaverProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK MyListBoxProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_KEYDOWN:
		if (wParam == VK_DELETE)
		{
			PostMessage(GetParent(hWnd), WM_COMMAND, IDC_BUTTON_DELETE, 0);
		}
		else if (wParam == 'A' && GetAsyncKeyState(VK_CONTROL) < 0)
		{
			SendDlgItemMessage(GetParent(hWnd), IDC_VIDEO_LIST, LB_SETSEL, 1, -1);
		}
		break;
	}
	return CallWindowProc(DefaultListBoxWndProc, hWnd, msg, wParam, lParam);
}

BOOL WINAPI ScreenSaverConfigureDialog(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static Setting setting;
	switch (msg)
	{
	case WM_INITDIALOG:
		{
			//SetThreadUILanguage(MAKELANGID(LANG_ENGLISH, SUBLANG_NEUTRAL));
			WCHAR szText[256];
			LoadString(0, IDS_STRING500, szText, _countof(szText));
			SetWindowText(hWnd, szText);
			LoadString(0, IDS_STRING501, szText, _countof(szText));
			SetDlgItemText(hWnd, IDC_STATIC_VIDEO_SPECIFICATION, szText);
			LoadString(0, IDS_STRING502, szText, _countof(szText));
			SetDlgItemText(hWnd, IDC_BUTTON_ADD, szText);
			LoadString(0, IDS_STRING503, szText, _countof(szText));
			SetDlgItemText(hWnd, IDC_BUTTON_DELETE, szText);
			LoadString(0, IDS_STRING504, szText, _countof(szText));
			SetDlgItemText(hWnd, IDC_CHECK_MUTE, szText);
			LoadString(0, IDS_STRING505, szText, _countof(szText));
			SetDlgItemText(hWnd, IDC_CHECK_RANDOM, szText);
			LoadString(0, IDS_STRING506, szText, _countof(szText));
			SetDlgItemText(hWnd, IDC_STATIC_VERSION, szText);
			LoadString(0, IDS_STRING507, szText, _countof(szText));
			SetDlgItemText(hWnd, IDOK, szText);
			LoadString(0, IDS_STRING508, szText, _countof(szText));
			SetDlgItemText(hWnd, IDCANCEL, szText);
		}
		setting.Load();
		{
			const int nFilePathCount = setting.GetFilePathCount();
			for (int i = 0; i < nFilePathCount; ++i)
			{
				LPCTSTR lpszFilePath = setting.GetFilePath(i);
				if (lpszFilePath)
				{
					const int nIndex = (int)SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_ADDSTRING, 0, (LPARAM)lpszFilePath);
					SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_SETSEL, 1, nIndex);
				}
			}
			PostMessage(hWnd, WM_COMMAND, MAKEWPARAM(IDC_VIDEO_LIST, LBN_SELCHANGE), 0);
		}
		SendDlgItemMessage(hWnd, IDC_CHECK_MUTE, BM_SETCHECK, setting.GetMute() ? BST_CHECKED : BST_UNCHECKED, 0);
		SendDlgItemMessage(hWnd, IDC_CHECK_RANDOM, BM_SETCHECK, setting.GetRandom() ? BST_CHECKED : BST_UNCHECKED, 0);
		ChangeWindowMessageFilterEx(hWnd, WM_DROPFILES, MSGFLT_ALLOW, 0);
		ChangeWindowMessageFilterEx(hWnd, /*WM_COPYGLOBALDATA*/ 0x0049, MSGFLT_ALLOW, 0);
		DefaultListBoxWndProc = (WNDPROC)SetWindowLongPtr(GetDlgItem(hWnd, IDC_VIDEO_LIST), GWLP_WNDPROC, (LONG_PTR)MyListBoxProc);
		return TRUE;
	case WM_DROPFILES:
	{
		SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_SETSEL, 0, -1);
		const UINT nFileCount = DragQueryFile((HDROP)wParam, 0xFFFFFFFF, NULL, 0);
		for (UINT i = 0; i < nFileCount; ++i)
		{
			WCHAR szFilePath[MAX_PATH];
			DragQueryFile((HDROP)wParam, i, szFilePath, _countof(szFilePath));
			if (PathMatchSpec(szFilePath, L"*.avi;*.mpg;*.wmv;*.mp4;*.mov;"))
			{
				const int nIndex = (int)SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_ADDSTRING, 0, (LPARAM)szFilePath);
				SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_SETSEL, 1, nIndex);
			}
		}
		DragFinish((HDROP)wParam);
		PostMessage(hWnd, WM_COMMAND, MAKEWPARAM(IDC_VIDEO_LIST, LBN_SELCHANGE), 0);
	}
	return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_BUTTON_ADD:
		{
#define MAX_CFileDialog_FILE_COUNT 99
#define FILE_LIST_BUFFER_SIZE ((MAX_CFileDialog_FILE_COUNT * (MAX_PATH + 1)) + 1)
			LPTSTR lpszFilePath = (LPTSTR)GlobalAlloc(GMEM_ZEROINIT, sizeof(WCHAR) * FILE_LIST_BUFFER_SIZE);
			OPENFILENAME of = { 0 };
			of.lStructSize = sizeof(OPENFILENAME);
			of.hwndOwner = hWnd;
			of.lpstrFilter = L"動画ファイル\0*.avi;*.mpg;*.wmv;*.mp4;*.mov;\0すべてのファイル (*.*)\0*.*\0\0";
			of.lpstrFile = lpszFilePath;
			of.nMaxFile = FILE_LIST_BUFFER_SIZE;
			of.nMaxFileTitle = MAX_PATH;
			of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
			of.lpstrTitle = L"動画ファイルの指定";
			if (GetOpenFileName(&of))
			{
				SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_SETSEL, 0, -1);
				if (PathIsDirectory(lpszFilePath))
				{
					WCHAR szDirectory[MAX_PATH];
					lstrcpy(szDirectory, lpszFilePath);
					LPTSTR p = lpszFilePath;
					while (*(p += lstrlen(p) + 1) != L'\0')
					{
						WCHAR szFilePath[MAX_PATH];
						lstrcpy(szFilePath, szDirectory);
						PathAppend(szFilePath, p);
						const int nIndex = (int)SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_ADDSTRING, 0, (LPARAM)szFilePath);
						SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_SETSEL, 1, nIndex);
					}
				}
				else if (PathFileExists(lpszFilePath))
				{
					const int nIndex = (int)SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_ADDSTRING, 0, (LPARAM)lpszFilePath);
					SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_SETSEL, 1, nIndex);
				}
				PostMessage(hWnd, WM_COMMAND, MAKEWPARAM(IDC_VIDEO_LIST, LBN_SELCHANGE), 0);
			}
			GlobalFree(lpszFilePath);
		}
		return TRUE;
		case IDC_BUTTON_DELETE:
		{
			const int nSelCount = (int)SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_GETSELCOUNT, 0, 0);
			if (nSelCount > 0)
			{
				int* nSelItems = (int*)GlobalAlloc(0, sizeof(int) * nSelCount);
				SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_GETSELITEMS, nSelCount, (LPARAM)nSelItems);
				for (int i = nSelCount - 1; i >= 0; --i)
				{
					SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_DELETESTRING, nSelItems[i], 0);
				}
				const int nGetCount = SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_GETCOUNT, 0, 0);
				if (nGetCount > 0)
				{
					SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_SETSEL, TRUE, (nSelCount == 1) ? min(nSelItems[0], nGetCount - 1) : 0);
					SetFocus(GetDlgItem(hWnd, IDC_VIDEO_LIST));
				}
				GlobalFree(nSelItems);
				PostMessage(hWnd, WM_COMMAND, MAKEWPARAM(IDC_VIDEO_LIST, LBN_SELCHANGE), 0);
			}
		}
		return TRUE;
		case IDC_VIDEO_LIST:
			if (HIWORD(wParam) == LBN_SELCHANGE)
			{
				const int nSelCount = (int)SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_GETSELCOUNT, 0, 0);
				EnableWindow(GetDlgItem(hWnd, IDC_BUTTON_DELETE), nSelCount > 0);
			}
			return TRUE;
		case IDOK:
		{
			WCHAR szFilePath[MAX_PATH];
			GetDlgItemText(hWnd, IDC_EDIT1, szFilePath, _countof(szFilePath));
			PathUnquoteSpaces(szFilePath);
			setting.ClearFilePath();
			{
				const int nItemCount = (int)SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_GETCOUNT, 0, 0);
				for (int i = 0; i < nItemCount; ++i)
				{
					WCHAR szFilePath[MAX_PATH];
					SendDlgItemMessage(hWnd, IDC_VIDEO_LIST, LB_GETTEXT, i, (LPARAM)szFilePath);
					setting.AddFilePath(szFilePath);
				}
			}
			setting.SetMute((BOOL)SendDlgItemMessage(hWnd, IDC_CHECK_MUTE, BM_GETCHECK, 0, 0));
			setting.SetRandom((BOOL)SendDlgItemMessage(hWnd, IDC_CHECK_RANDOM, BM_GETCHECK, 0, 0));
			setting.Save();
			EndDialog(hWnd, IDOK);
		}
		return TRUE;
		case IDCANCEL:
			EndDialog(hWnd, IDCANCEL);
			return TRUE;
		}
		return FALSE;
	}
	return FALSE;
}

BOOL WINAPI RegisterDialogClasses(HANDLE hInst)
{
	return TRUE;
}
