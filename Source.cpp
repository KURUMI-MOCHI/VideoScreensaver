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
#include "resource.h"

CComModule _Module;

// --- 設定クラス（保存・読み込みを完全固定） ---
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

// --- プロシージャ ---
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
        
        // 【重要】WMPが起動しきるまで100ms待つ
        SetTimer(hWnd, 100, 100, NULL);
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
                    // 1. メイン画面判定と音量設定
                    RECT rc; GetWindowRect(hWnd, &rc);
                    CComPtr<IWMPSettings> spSet;
                    spPlayer->get_settings(&spSet);
                    if (spSet) {
                        // 左上が(0,0)かつプレビューでない場合のみ音を出す
                        BOOL bMute = (rc.left == 0 && rc.top == 0 && !bPreview && !setting.GetMute()) ? FALSE : TRUE;
                        spSet->put_mute(bMute ? VARIANT_TRUE : VARIANT_FALSE);
                        spSet->put_volume(50);
                    }

                    // 2. 動画再生
                    int idx = 0;
                    if (setting.GetRandom() && setting.GetFilePathCount() > 1) {
                        std::random_device rd; std::mt19937 g(rd());
                        std::uniform_int_distribution<int> d(0, setting.GetFilePathCount() - 1);
                        idx = d(g);
                    }
                    spPlayer->put_URL(CComBSTR(setting.GetFilePath(idx)));
                    
                    // 3. 念のため明示的に Play 命令
                    CComPtr<IWMPControls> spCtrl;
                    if (SUCCEEDED(spPlayer->get_controls(&spCtrl))) spCtrl->play();
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
