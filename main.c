/**
 * Git GUI 客户端 - 使用 C 语言和 Win32 API
 * 支持代码提交、查看日志、Push/Pull 等常用功能
 */

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "resource.h"
#include "git_operations.h"
#include "ui_callbacks.h"

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif

AppState g_appState;

#define WINDOW_WIDTH    900
#define WINDOW_HEIGHT   650
#define STATUSBAR_HEIGHT 25
#define TAB_HEIGHT      30

HWND g_hBtnSelectRepo = NULL;
HWND g_hBtnRefresh = NULL;
HWND g_hBtnPush = NULL;
HWND g_hBtnPull = NULL;
HWND g_hBtnSettings = NULL;

static int g_toolbarH = 40;
static HFONT g_hMainFont = NULL;

// 前向声明
void InitListViewColumns(HWND hList, int type);

HFONT CreateUIFont(void) {
    if (g_hMainFont) {
        return g_hMainFont;
    }
    HDC hdc = GetDC(NULL);
    int height = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(NULL, hdc);
    g_hMainFont = CreateFont(
        height,
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        TEXT("Microsoft YaHei")
    );
    return g_hMainFont;
}

static int TextWidth(HDC hdc, const TCHAR* text) {
    SIZE sz;
    GetTextExtentPoint32(hdc, text, (int)wcslen(text), &sz);
    return sz.cx;
}

static int BtnWidth(HDC hdc, const TCHAR* text) {
    return TextWidth(hdc, text) + 24;
}

void CreateControls(HWND hWnd) {
    HFONT hFont = CreateUIFont();
    RECT rcClient;
    GetClientRect(hWnd, &rcClient);

    HDC hdc = GetDC(hWnd);
    HGDIOBJ hOld = SelectObject(hdc, hFont);

    int btnGap = 8;
    int btnH = 30;
    int btnY = 5;
    int btnX = 10;

    struct {
        const TCHAR* text;
        HWND phwnd;
        int id;
    } topBtns[] = {
        { TEXT("选择仓库"), g_hBtnSelectRepo, ID_BTN_SELECT_REPO },
        { TEXT("刷新"),     g_hBtnRefresh,     ID_BTN_REFRESH },
        { TEXT("推送"),     g_hBtnPush,        ID_BTN_PUSH },
        { TEXT("拉取"),     g_hBtnPull,        ID_BTN_PULL },
        { TEXT("设置"),     g_hBtnSettings,    ID_BTN_SETTINGS },
    };
    int n = sizeof(topBtns) / sizeof(topBtns[0]);
    int totalW = 0;
    for (int i = 0; i < n; i++) {
        int w = BtnWidth(hdc, topBtns[i].text);
        totalW += w + btnGap;
    }

    for (int i = 0; i < n; i++) {
        int w = BtnWidth(hdc, topBtns[i].text);
        topBtns[i].phwnd = CreateWindowEx(0, TEXT("BUTTON"), topBtns[i].text,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            btnX, btnY, w, btnH, hWnd, (HMENU)(INT_PTR)topBtns[i].id, NULL, NULL);
        SendMessage(topBtns[i].phwnd, WM_SETFONT, (WPARAM)hFont, TRUE);
        btnX += w + btnGap;
    }

    g_hBtnSelectRepo = topBtns[0].phwnd;
    g_hBtnRefresh = topBtns[1].phwnd;
    g_hBtnPush = topBtns[2].phwnd;
    g_hBtnPull = topBtns[3].phwnd;
    g_hBtnSettings = topBtns[4].phwnd;

    int toolbarH = btnY + btnH + 5;
    g_toolbarH = toolbarH;

    g_appState.hTabControl = CreateWindowEx(0, WC_TABCONTROL, TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        5, toolbarH,
        rcClient.right - 10, TAB_HEIGHT,
        hWnd, (HMENU)ID_TAB_CONTROL, NULL, NULL);
    SendMessage(g_appState.hTabControl, WM_SETFONT, (WPARAM)hFont, TRUE);

    TCITEM tie;
    tie.mask = TCIF_TEXT;
    tie.pszText = TEXT("状态");
    TabCtrl_InsertItem(g_appState.hTabControl, 0, &tie);
    tie.pszText = TEXT("日志");
    TabCtrl_InsertItem(g_appState.hTabControl, 1, &tie);

    int contentTop = toolbarH + TAB_HEIGHT + 5;
    int halfW = (rcClient.right - 15) / 2;
    int rightX = halfW + 15;

    g_appState.hFileList = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, TEXT(""),
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
        5, contentTop,
        halfW, rcClient.bottom - toolbarH - TAB_HEIGHT - STATUSBAR_HEIGHT - 10,
        hWnd, (HMENU)ID_FILE_LIST, NULL, NULL);
    SendMessage(g_appState.hFileList, WM_SETFONT, (WPARAM)hFont, TRUE);
    ListView_SetExtendedListViewStyle(g_appState.hFileList, LVS_EX_FULLROWSELECT);

    InitListViewColumns(g_appState.hFileList, 0);

    g_appState.hLabelCommit = CreateWindowEx(0, TEXT("STATIC"), TEXT("提交信息:"),
        WS_CHILD | WS_VISIBLE,
        rightX, contentTop + 5,
        BtnWidth(hdc, TEXT("提交信息:")), 20, hWnd, NULL, NULL, NULL);
    SendMessage(g_appState.hLabelCommit, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_appState.hCommitMsg = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""),
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
        rightX, contentTop + 30,
        rcClient.right - rightX - 10, 80,
        hWnd, (HMENU)ID_COMMIT_MSG, NULL, NULL);
    SendMessage(g_appState.hCommitMsg, WM_SETFONT, (WPARAM)hFont, TRUE);

    struct {
        const TCHAR* text;
        HWND phwnd;
        int id;
    } rightBtns[] = {
        { TEXT("暂存选中"),   NULL, ID_BTN_STAGE },
        { TEXT("暂存全部"),   NULL, ID_BTN_STAGE_ALL },
        { TEXT("提交更改"),   NULL, ID_BTN_COMMIT_NOW },
    };
    int nr = sizeof(rightBtns) / sizeof(rightBtns[0]);
    int rBtnW[3], rBtnGap = 8;
    for (int i = 0; i < nr; i++) {
        rBtnW[i] = BtnWidth(hdc, rightBtns[i].text);
    }

    int rBtnY = contentTop + 120;
    int rBtnX = rightX;
    for (int i = 0; i < nr; i++) {
        rightBtns[i].phwnd = CreateWindowEx(0, TEXT("BUTTON"), rightBtns[i].text,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            rBtnX, rBtnY, rBtnW[i], 28, hWnd, (HMENU)(INT_PTR)rightBtns[i].id, NULL, NULL);
        SendMessage(rightBtns[i].phwnd, WM_SETFONT, (WPARAM)hFont, TRUE);
        rBtnX += rBtnW[i] + rBtnGap;
    }

    g_appState.hBtnStage = rightBtns[0].phwnd;
    g_appState.hBtnStageAll = rightBtns[1].phwnd;
    g_appState.hBtnCommitNow = rightBtns[2].phwnd;

    g_appState.hLabelOutput = CreateWindowEx(0, TEXT("STATIC"), TEXT("输出:"),
        WS_CHILD | WS_VISIBLE,
        rightX, contentTop + 160,
        BtnWidth(hdc, TEXT("输出:")), 20, hWnd, NULL, NULL, NULL);
    SendMessage(g_appState.hLabelOutput, WM_SETFONT, (WPARAM)hFont, TRUE);

    int outputTop = contentTop + 185;
    g_appState.hOutput = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""),
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY,
        rightX, outputTop,
        rcClient.right - rightX - 10, rcClient.bottom - toolbarH - TAB_HEIGHT - STATUSBAR_HEIGHT - outputTop - 10,
        hWnd, (HMENU)ID_OUTPUT, NULL, NULL);
    SendMessage(g_appState.hOutput, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_appState.hLogList = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, TEXT(""),
        WS_CHILD | LVS_REPORT | LVS_SINGLESEL,
        5, contentTop,
        halfW, rcClient.bottom - toolbarH - TAB_HEIGHT - STATUSBAR_HEIGHT - 15,
        hWnd, (HMENU)ID_LOG_LIST, NULL, NULL);
    SendMessage(g_appState.hLogList, WM_SETFONT, (WPARAM)hFont, TRUE);
    ListView_SetExtendedListViewStyle(g_appState.hLogList, LVS_EX_FULLROWSELECT);

    InitListViewColumns(g_appState.hLogList, 1);

    g_appState.hCommitFileList = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, TEXT(""),
        WS_CHILD | LVS_REPORT | LVS_SINGLESEL,
        halfW + 10, contentTop,
        halfW - 5, rcClient.bottom - toolbarH - TAB_HEIGHT - STATUSBAR_HEIGHT - 15,
        hWnd, (HMENU)ID_COMMIT_FILE_LIST, NULL, NULL);
    SendMessage(g_appState.hCommitFileList, WM_SETFONT, (WPARAM)hFont, TRUE);

    InitListViewColumns(g_appState.hCommitFileList, 2);

    g_appState.hStatusBar = CreateWindowEx(0, STATUSCLASSNAME, TEXT(""),
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, rcClient.bottom - STATUSBAR_HEIGHT,
        rcClient.right, STATUSBAR_HEIGHT,
        hWnd, (HMENU)ID_STATUS_BAR, NULL, NULL);
    SendMessage(g_appState.hStatusBar, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_appState.hMainWnd = hWnd;

    int minClientW = totalW + 20;
    if (rcClient.right < minClientW) {
        RECT rc = { 0, 0, minClientW, rcClient.bottom };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(hWnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);
    }

    SelectObject(hdc, hOld);
    ReleaseDC(hWnd, hdc);
}

void InitListViewColumns(HWND hList, int type) {
    LVCOLUMN lvc;
    memset(&lvc, 0, sizeof(lvc));
    lvc.mask = LVCF_TEXT | LVCF_WIDTH;

    if (type == 0) {
        lvc.pszText = TEXT("文件");
        lvc.cx = 300;
        ListView_InsertColumn(hList, 0, &lvc);

        lvc.pszText = TEXT("状态");
        lvc.cx = 80;
        ListView_InsertColumn(hList, 1, &lvc);

        lvc.pszText = TEXT("已暂存");
        lvc.cx = 60;
        ListView_InsertColumn(hList, 2, &lvc);
    } else if (type == 1) {
        lvc.pszText = TEXT("哈希");
        lvc.cx = 80;
        ListView_InsertColumn(hList, 0, &lvc);

        lvc.pszText = TEXT("提交信息");
        lvc.cx = 400;
        ListView_InsertColumn(hList, 1, &lvc);

        lvc.pszText = TEXT("作者");
        lvc.cx = 100;
        ListView_InsertColumn(hList, 2, &lvc);

        lvc.pszText = TEXT("日期");
        lvc.cx = 100;
        ListView_InsertColumn(hList, 3, &lvc);
    } else if (type == 2) {
        lvc.pszText = TEXT("文件");
        lvc.cx = 300;
        ListView_InsertColumn(hList, 0, &lvc);

        lvc.pszText = TEXT("状态");
        lvc.cx = 80;
        ListView_InsertColumn(hList, 1, &lvc);
    }
}

void ResizeControls(HWND hWnd) {
    RECT rcClient;
    GetClientRect(hWnd, &rcClient);

    int contentTop = g_toolbarH + TAB_HEIGHT + 5;
    int contentHeight = rcClient.bottom - g_toolbarH - TAB_HEIGHT - STATUSBAR_HEIGHT - 10;
    int halfWidth = (rcClient.right - 15) / 2;
    int rightX = halfWidth + 15;

    if (g_appState.hFileList) {
        SetWindowPos(g_appState.hFileList, NULL,
            5, contentTop,
            halfWidth, contentHeight,
            SWP_NOZORDER);
    }

    if (g_appState.hLabelCommit) {
        SetWindowPos(g_appState.hLabelCommit, NULL, rightX, contentTop + 5, 80, 20, SWP_NOZORDER);
    }

    if (g_appState.hCommitMsg) {
        SetWindowPos(g_appState.hCommitMsg, NULL,
            rightX, contentTop + 30,
            rcClient.right - rightX - 10, 80,
            SWP_NOZORDER);
    }

    if (g_appState.hBtnStage) {
        SetWindowPos(g_appState.hBtnStage, NULL, rightX, contentTop + 120, 80, 28, SWP_NOZORDER);
    }

    if (g_appState.hBtnStageAll) {
        SetWindowPos(g_appState.hBtnStageAll, NULL, rightX + 85, contentTop + 120, 80, 28, SWP_NOZORDER);
    }

    if (g_appState.hBtnCommitNow) {
        SetWindowPos(g_appState.hBtnCommitNow, NULL, rightX + 170, contentTop + 120, 80, 28, SWP_NOZORDER);
    }

    if (g_appState.hLabelOutput) {
        SetWindowPos(g_appState.hLabelOutput, NULL, rightX, contentTop + 160, 80, 20, SWP_NOZORDER);
    }

    if (g_appState.hOutput) {
        SetWindowPos(g_appState.hOutput, NULL,
            rightX, contentTop + 185,
            rcClient.right - rightX - 10, contentHeight - 185,
            SWP_NOZORDER);
    }

    if (g_appState.hLogList) {
        SetWindowPos(g_appState.hLogList, NULL,
            5, contentTop,
            halfWidth, contentHeight,
            SWP_NOZORDER);
    }

    if (g_appState.hCommitFileList) {
        SetWindowPos(g_appState.hCommitFileList, NULL,
            halfWidth + 10, contentTop,
            halfWidth - 5, contentHeight,
            SWP_NOZORDER);
    }

    if (g_appState.hTabControl) {
        SetWindowPos(g_appState.hTabControl, NULL,
            5, g_toolbarH,
            rcClient.right - 10, TAB_HEIGHT,
            SWP_NOZORDER);
    }

    if (g_appState.hStatusBar) {
        SetWindowPos(g_appState.hStatusBar, NULL,
            0, rcClient.bottom - STATUSBAR_HEIGHT,
            rcClient.right, STATUSBAR_HEIGHT,
            SWP_NOZORDER);
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            INITCOMMONCONTROLSEX icex;
            icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
            icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES;
            InitCommonControlsEx(&icex);

            app_state_init(&g_appState);
            g_appState.hMainWnd = hWnd;
            strncpy(g_git_path, g_appState.git_path, MAX_PATH_LEN - 1);
            g_git_path[MAX_PATH_LEN - 1] = '\0';

            CreateControls(hWnd);

            if (strcmp(g_appState.repo_path, ".") == 0) {
                GetCurrentDirectoryA(MAX_PATH_LEN, g_appState.repo_path);
            }
            if (git_is_repository(g_appState.repo_path)) {
                refresh_branch_info(&g_appState);
                TabCtrl_SetCurSel(g_appState.hTabControl, 1);
                on_tab_changed(&g_appState, 1);
            }
            break;

        case WM_SIZE:
            ResizeControls(hWnd);
            break;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_BTN_COMMIT_NOW:
                    on_commit_clicked(&g_appState);
                    break;

                case ID_BTN_PUSH:
                    on_push_clicked(&g_appState);
                    break;

                case ID_BTN_PULL:
                    on_pull_clicked(&g_appState);
                    break;

                case ID_BTN_REFRESH:
                    on_refresh_clicked(&g_appState);
                    break;

                case ID_BTN_STAGE:
                    on_stage_clicked(&g_appState);
                    break;

                case ID_BTN_STAGE_ALL:
                    on_stage_all_clicked(&g_appState);
                    break;

                case ID_BTN_SELECT_REPO:
                    on_select_repo_clicked(&g_appState);
                    break;

                case ID_BTN_SETTINGS:
                    on_settings_clicked(&g_appState);
                    strncpy(g_git_path, g_appState.git_path, MAX_PATH_LEN - 1);
                    g_git_path[MAX_PATH_LEN - 1] = '\0';
                    break;

                case IDM_FILE_EXIT:
                    PostQuitMessage(0);
                    break;

                case IDM_HELP_ABOUT:
                    MessageBox(hWnd,
                        TEXT("Git GUI 客户端\n版本 1.0\n\n使用 C 语言和 Win32 API 开发"),
                        TEXT("关于"),
                        MB_ICONINFORMATION);
                    break;
            }
            break;

        case WM_NOTIFY:
            {
                LPNMHDR lpnmhdr = (LPNMHDR)lParam;
                if (lpnmhdr->idFrom == ID_TAB_CONTROL) {
                    if (lpnmhdr->code == TCN_SELCHANGE) {
                        int tabIndex = TabCtrl_GetCurSel(g_appState.hTabControl);
                        on_tab_changed(&g_appState, tabIndex);
                    }
                }
                else if (lpnmhdr->idFrom == ID_FILE_LIST) {
                    if (lpnmhdr->code == NM_DBLCLK) {
                        on_file_double_click(&g_appState);
                    }
                    else if (lpnmhdr->code == NM_RCLICK) {
                        NMITEMACTIVATE* pnmia = (NMITEMACTIVATE*)lParam;
                        POINT pt = pnmia->ptAction;
                        ClientToScreen(g_appState.hFileList, &pt);
                        on_file_list_context_menu(&g_appState, pt);
                    }
                }
                else if (lpnmhdr->idFrom == ID_LOG_LIST) {
                    if (lpnmhdr->code == NM_CLICK) {
                        on_log_double_click(&g_appState);
                    }
                    else if (lpnmhdr->code == NM_RCLICK) {
                        NMITEMACTIVATE* pnmia = (NMITEMACTIVATE*)lParam;
                        POINT pt = pnmia->ptAction;
                        ClientToScreen(g_appState.hLogList, &pt);
                        on_log_list_context_menu(&g_appState, pt);
                    }
                }
                else if (lpnmhdr->idFrom == ID_COMMIT_FILE_LIST) {
                    if (lpnmhdr->code == NM_DBLCLK) {
                        on_commit_file_double_click(&g_appState);
                    }
                }
            }
            break;

        case WM_DESTROY:
            app_state_cleanup(&g_appState);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 启用 System DPI 感知，让 Windows 自动缩放整个程序（等同于"系统增强"效果）
    // 使用 GDI 缩放模式，字体清晰且布局正确
    typedef DPI_AWARENESS_CONTEXT (WINAPI *SetProcessDpiAwarenessContext_t)(DPI_AWARENESS_CONTEXT);
    HMODULE hUser32 = GetModuleHandle(TEXT("user32.dll"));
    if (hUser32) {
        SetProcessDpiAwarenessContext_t pSetProcessDpiAwarenessContext = 
            (SetProcessDpiAwarenessContext_t)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetProcessDpiAwarenessContext) {
            // DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED = ((DPI_AWARENESS_CONTEXT)-5)
            // 效果等同于兼容性设置中的"系统增强"
            pSetProcessDpiAwarenessContext((DPI_AWARENESS_CONTEXT)-5);
        } else {
            // Windows 8.1+ 兼容方案
            typedef BOOL (WINAPI *SetProcessDpiAwareness_t)(int);
            SetProcessDpiAwareness_t pSetProcessDpiAwareness = 
                (SetProcessDpiAwareness_t)GetProcAddress(hUser32, "SetProcessDpiAwareness");
            if (pSetProcessDpiAwareness) {
                pSetProcessDpiAwareness(1); // PROCESS_SYSTEM_DPI_AWARE
            } else {
                // Windows Vista/7 兼容方案
                typedef BOOL (WINAPI *SetProcessDPIAware_t)(void);
                SetProcessDPIAware_t pSetProcessDPIAware = 
                    (SetProcessDPIAware_t)GetProcAddress(hUser32, "SetProcessDPIAware");
                if (pSetProcessDPIAware) {
                    pSetProcessDPIAware();
                }
            }
        }
    }

    WNDCLASSEX wcex;
    HWND hWnd;
    MSG msg;

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = TEXT("GitGUIClass");
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wcex)) {
        MessageBox(NULL, TEXT("窗口类注册失败"), TEXT("错误"), MB_ICONERROR);
        return 1;
    }
 
    hWnd = CreateWindowEx(
        0,
        TEXT("GitGUIClass"),
        TEXT("Git GUI 客户端"),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, hInstance, NULL
    );

    if (!hWnd) {
        MessageBox(NULL, TEXT("窗口创建失败"), TEXT("错误"), MB_ICONERROR);
        return 1;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
