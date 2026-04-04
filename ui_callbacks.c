#define COBJMACROS
#define INITGUID
#include "ui_callbacks.h"
#include "resource.h"
#include <commctrl.h>
#include <shobjidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>

HFONT CreateUIFont(void);

void config_save(AppState* state);
void config_save_repo(AppState* state);

// 临时文件管理
#define MAX_TEMP_FILES 64
static TCHAR g_tempFiles[MAX_TEMP_FILES][MAX_PATH_LEN];
static int g_tempFileCount = 0;

static void register_temp_file(const TCHAR* path) {
    if (g_tempFileCount < MAX_TEMP_FILES) {
        _tcsncpy_s(g_tempFiles[g_tempFileCount], MAX_PATH_LEN, path, _TRUNCATE);
        g_tempFileCount++;
    }
}

static void cleanup_temp_files(void) {
    for (int i = 0; i < g_tempFileCount; i++) {
        DeleteFile(g_tempFiles[i]);
    }
    g_tempFileCount = 0;
}

typedef struct InputBoxCtx {
    const TCHAR* prompt;
    TCHAR* buf;
    int bufSize;
    HWND hEdit;
    HWND hDlg;
    HFONT hFont;
} InputBoxCtx;

static LRESULT CALLBACK InputBoxWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    InputBoxCtx* ctx = (InputBoxCtx*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    switch (msg) {
        case WM_CREATE: {
            LPCREATESTRUCT cs = (LPCREATESTRUCT)lParam;
            ctx = (InputBoxCtx*)cs->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)ctx);
            ctx->hDlg = hWnd;
            ctx->hFont = CreateUIFont();
            CreateWindowEx(0, TEXT("STATIC"), ctx->prompt,
                WS_CHILD | WS_VISIBLE, 10, 10, 290, 20, hWnd, NULL, NULL, NULL);
            ctx->hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 10, 32, 290, 22, hWnd, NULL, NULL, NULL);
            CreateWindowEx(0, TEXT("BUTTON"), TEXT("确定"),
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 120, 72, 80, 26, hWnd, (HMENU)IDOK, NULL, NULL);
            CreateWindowEx(0, TEXT("BUTTON"), TEXT("取消"),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 210, 72, 80, 26, hWnd, (HMENU)IDCANCEL, NULL, NULL);
            HWND hChild = GetWindow(hWnd, GW_CHILD);
            while (hChild) {
                SendMessage(hChild, WM_SETFONT, (WPARAM)ctx->hFont, FALSE);
                hChild = GetWindow(hChild, GW_HWNDNEXT);
            }
            SetFocus(ctx->hEdit);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                GetWindowText(ctx->hEdit, ctx->buf, ctx->bufSize);
                if (ctx->buf[0] != '\0') {
                    DestroyWindow(hWnd);
                }
            } else if (LOWORD(wParam) == IDCANCEL) {
                ctx->buf[0] = '\0';
                DestroyWindow(hWnd);
            }
            break;
        case WM_CLOSE:
            ctx->buf[0] = '\0';
            DestroyWindow(hWnd);
            break;
        case WM_DESTROY:
            if (ctx && ctx->hFont) DeleteObject(ctx->hFont);
            break;
        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

static BOOL InputBox(HWND hParent, const TCHAR* prompt, const TCHAR* title, TCHAR* buf, int bufSize) {
    static WNDCLASSEX wcex = {0};
    if (!wcex.cbSize) {
        wcex.cbSize = sizeof(WNDCLASSEX);
        wcex.lpfnWndProc = InputBoxWndProc;
        wcex.hInstance = GetModuleHandle(NULL);
        wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wcex.lpszClassName = TEXT("GitGUIInputBoxClass");
        wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassEx(&wcex);
    }
    buf[0] = '\0';
    InputBoxCtx ctx;
    ctx.prompt = prompt;
    ctx.buf = buf;
    ctx.bufSize = bufSize;
    ctx.hEdit = NULL;
    ctx.hDlg = NULL;
    ctx.hFont = NULL;
    int dlgW = 320, dlgH = 130;
    RECT rc = { 0, 0, dlgW, dlgH };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;
    RECT mainRc;
    GetWindowRect(hParent, &mainRc);
    int x = mainRc.left + (mainRc.right - mainRc.left - winW) / 2;
    int y = mainRc.top + (mainRc.bottom - mainRc.top - winH) / 2;
    CreateWindowEx(WS_EX_DLGMODALFRAME, TEXT("GitGUIInputBoxClass"),
        title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, winW, winH, hParent, NULL, GetModuleHandle(NULL), &ctx);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) && msg.hwnd != ctx.hDlg) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return buf[0] != '\0';
}

void app_state_init(AppState* state) {
    memset(state, 0, sizeof(AppState));
    strcpy(state->repo_path, ".");
    strcpy(state->current_branch, "unknown");
    strcpy(state->git_path, "git.exe");
    config_load(state);
}

void app_state_cleanup(AppState* state) {
    if (state->file_list) {
        git_free_file_list(state->file_list);
        state->file_list = NULL;
    }
    if (state->commit_list) {
        git_free_commit_list(state->commit_list);
        state->commit_list = NULL;
    }
    if (state->commit_file_list) {
        git_free_commit_file_list(state->commit_file_list);
        state->commit_file_list = NULL;
    }
    // 清理临时文件
    cleanup_temp_files();
}

void append_output(AppState* state, const TCHAR* text) {
    if (!state->hOutput) return;
    
    int len = GetWindowTextLength(state->hOutput);
    SendMessage(state->hOutput, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(state->hOutput, EM_REPLACESEL, (WPARAM)FALSE, (LPARAM)text);
    SendMessage(state->hOutput, EM_SCROLLCARET, 0, 0);
}

void append_output_line(AppState* state, const TCHAR* text) {
    if (!state->hOutput) return;
    
    append_output(state, text);
    SendMessage(state->hOutput, EM_REPLACESEL, (WPARAM)FALSE, (LPARAM)TEXT("\r\n"));
    SendMessage(state->hOutput, EM_SCROLLCARET, 0, 0);
}

void clear_output(AppState* state) {
    if (state->hOutput) {
        SetWindowText(state->hOutput, TEXT(""));
    }
}

static void get_status_text(FileStatus status, TCHAR* buf, int size) {
    switch (status) {
        case FILE_STATUS_MODIFIED:  _tcscpy_s(buf, size, TEXT("已修改")); break;
        case FILE_STATUS_ADDED:     _tcscpy_s(buf, size, TEXT("已增加")); break;
        case FILE_STATUS_DELETED:   _tcscpy_s(buf, size, TEXT("已删除")); break;
        case FILE_STATUS_UNTRACKED: _tcscpy_s(buf, size, TEXT("未跟踪")); break;
        case FILE_STATUS_STAGED:    _tcscpy_s(buf, size, TEXT("已暂存")); break;
        default:                    _tcscpy_s(buf, size, TEXT("未知")); break;
    }
}

void refresh_file_list(AppState* state) {
    if (!state->hFileList) return;

    // 先清除 ListView 中所有项的 lParam，避免悬空指针
    int itemCount = ListView_GetItemCount(state->hFileList);
    for (int i = 0; i < itemCount; i++) {
        LVITEM lvi;
        lvi.mask = LVIF_PARAM;
        lvi.iItem = i;
        lvi.lParam = 0;
        ListView_SetItem(state->hFileList, &lvi);
    }

    ListView_DeleteAllItems(state->hFileList);

    if (state->file_list) {
        git_free_file_list(state->file_list);
        state->file_list = NULL;
    }

    state->file_list = git_get_status(state->repo_path);

    FileInfo* info = state->file_list;
    int index = 0;
    while (info) {
        TCHAR wpath[MAX_PATH_LEN];
        TCHAR wstatus[64];
        
        utf8_to_wide(info->path, wpath, MAX_PATH_LEN);
        get_status_text(info->status, wstatus, 64);

        LVITEM lvi;
        memset(&lvi, 0, sizeof(lvi));
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = index;
        lvi.lParam = (LPARAM)info;
        lvi.pszText = wpath;
        ListView_InsertItem(state->hFileList, &lvi);

        lvi.mask = LVIF_TEXT;
        lvi.iSubItem = 1;
        lvi.pszText = wstatus;
        ListView_SetItem(state->hFileList, &lvi);

        lvi.iSubItem = 2;
        lvi.pszText = info->staged ? TEXT("是") : TEXT("否");
        ListView_SetItem(state->hFileList, &lvi);

        info = info->next;
        index++;
    }

    ListView_SetColumnWidth(state->hFileList, 0, LVSCW_AUTOSIZE);
    ListView_SetColumnWidth(state->hFileList, 1, 80);
    ListView_SetColumnWidth(state->hFileList, 2, 60);
}

void refresh_log_list(AppState* state) {
    if (!state->hLogList) return;

    // 先清除 ListView 中所有项的 lParam，避免悬空指针
    int itemCount = ListView_GetItemCount(state->hLogList);
    for (int i = 0; i < itemCount; i++) {
        LVITEM lvi;
        lvi.mask = LVIF_PARAM;
        lvi.iItem = i;
        lvi.lParam = 0;
        ListView_SetItem(state->hLogList, &lvi);
    }

    ListView_DeleteAllItems(state->hLogList);

    if (state->commit_list) {
        git_free_commit_list(state->commit_list);
        state->commit_list = NULL;
    }

    state->commit_list = git_get_log(state->repo_path, 50);

    CommitInfo* info = state->commit_list;
    int index = 0;
    while (info) {
        TCHAR whash[32];
        TCHAR wmessage[1024];
        TCHAR wauthor[256];
        TCHAR wdate[128];

        utf8_to_wide(info->hash, whash, 32);
        utf8_to_wide(info->message, wmessage, 1024);
        utf8_to_wide(info->author, wauthor, 256);
        utf8_to_wide(info->date, wdate, 128);

        LVITEM lvi;
        memset(&lvi, 0, sizeof(lvi));
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = index;
        lvi.lParam = (LPARAM)info;
        lvi.pszText = whash;
        ListView_InsertItem(state->hLogList, &lvi);

        lvi.mask = LVIF_TEXT;
        lvi.iSubItem = 1;
        lvi.pszText = wmessage;
        ListView_SetItem(state->hLogList, &lvi);

        lvi.iSubItem = 2;
        lvi.pszText = wauthor;
        ListView_SetItem(state->hLogList, &lvi);

        lvi.iSubItem = 3;
        lvi.pszText = wdate;
        ListView_SetItem(state->hLogList, &lvi);

        info = info->next;
        index++;
    }

    ListView_SetColumnWidth(state->hLogList, 0, 80);
    ListView_SetColumnWidth(state->hLogList, 1, LVSCW_AUTOSIZE);
    ListView_SetColumnWidth(state->hLogList, 2, 100);
    ListView_SetColumnWidth(state->hLogList, 3, 100);
}

void refresh_branch_info(AppState* state) {
    git_get_current_branch(state->repo_path, state->current_branch, sizeof(state->current_branch));
    update_status_bar(state);
}

void update_status_bar(AppState* state) {
    if (state->hStatusBar) {
        TCHAR text[512];
        TCHAR wbranch[128];
        TCHAR wpath[MAX_PATH_LEN];
        
        utf8_to_wide(state->current_branch, wbranch, 128);
        utf8_to_wide(state->repo_path, wpath, MAX_PATH_LEN);
        
        _sntprintf_s(text, 512, _TRUNCATE, TEXT("分支: %s | 仓库: %s"), wbranch, wpath);
        SetWindowText(state->hStatusBar, text);
    }
}

void on_commit_clicked(AppState* state) {
    TCHAR wmessage[1024] = {0};
    char message[1024] = {0};

    if (!state->hCommitMsg) return;

    GetWindowText(state->hCommitMsg, wmessage, sizeof(wmessage) / sizeof(TCHAR));
    wide_to_utf8(wmessage, message, sizeof(message));

    if (strlen(message) == 0) {
        append_output_line(state, TEXT("[错误] 请输入提交信息"));
        return;
    }

    FileInfo* info = state->file_list;
    int hasStaged = 0;
    while (info) {
        if (info->staged) {
            hasStaged = 1;
            break;
        }
        info = info->next;
    }

    if (!hasStaged) {
        append_output_line(state, TEXT("[警告] 没有已暂存的文件，请先暂存要提交的文件"));
        return;
    }

    append_output_line(state, TEXT("[执行] git commit..."));
    int result = git_commit(state->repo_path, message);

    if (result == 0) {
        append_output_line(state, TEXT("[成功] 提交成功"));
        SetWindowText(state->hCommitMsg, TEXT(""));
        refresh_file_list(state);
        refresh_log_list(state);
    } else {
        append_output_line(state, TEXT("[错误] 提交失败，请检查是否有已暂存的更改"));
    }
}

void on_push_clicked(AppState* state) {
    char output[65536];
    TCHAR woutput[65536];

    memset(output, 0, sizeof(output));
    memset(woutput, 0, sizeof(woutput));

    append_output_line(state, TEXT("[执行] git push..."));
    int result = git_push(state->repo_path, output, sizeof(output));

    if (strlen(output) > 0) {
        utf8_to_wide(output, woutput, sizeof(woutput) / sizeof(TCHAR));
        append_output(state, woutput);
    }

    if (result == 0) {
        append_output_line(state, TEXT("[成功] 推送成功"));
    } else {
        append_output_line(state, TEXT("[错误] 推送失败"));
    }

    refresh_log_list(state);
}

void on_pull_clicked(AppState* state) {
    char output[65536];
    TCHAR woutput[65536];

    memset(output, 0, sizeof(output));
    memset(woutput, 0, sizeof(woutput));

    append_output_line(state, TEXT("[执行] git pull..."));
    int result = git_pull(state->repo_path, output, sizeof(output));

    if (strlen(output) > 0) {
        utf8_to_wide(output, woutput, sizeof(woutput) / sizeof(TCHAR));
        append_output(state, woutput);
    }

    if (result == 0) {
        append_output_line(state, TEXT("[成功] 拉取成功"));
        refresh_file_list(state);
        refresh_log_list(state);
    } else {
        append_output_line(state, TEXT("[错误] 拉取失败"));
    }
}

void on_refresh_clicked(AppState* state) {
    refresh_branch_info(state);
    refresh_file_list(state);
    refresh_log_list(state);
}

void on_stage_clicked(AppState* state) {
    if (!state->hFileList) return;

    int selectedIndex = ListView_GetNextItem(state->hFileList, -1, LVNI_SELECTED);

    if (selectedIndex == -1) {
        append_output_line(state, TEXT("[警告] 请先选择要暂存的文件"));
        return;
    }

    LVITEM lvi;
    memset(&lvi, 0, sizeof(lvi));
    lvi.mask = LVIF_PARAM;
    lvi.iItem = selectedIndex;
    ListView_GetItem(state->hFileList, &lvi);

    FileInfo* info = (FileInfo*)lvi.lParam;
    if (info) {
        TCHAR wpath[MAX_PATH_LEN];
        utf8_to_wide(info->path, wpath, MAX_PATH_LEN);
        TCHAR msg[MAX_PATH_LEN + 50];
        _sntprintf_s(msg, MAX_PATH_LEN + 50, _TRUNCATE, TEXT("[执行] git add \"%s\""), wpath);
        append_output_line(state, msg);

        int result = git_add(state->repo_path, info->path);
        if (result == 0) {
            append_output_line(state, TEXT("[成功] 暂存成功"));
            refresh_file_list(state);
        } else {
            append_output_line(state, TEXT("[错误] 暂存失败"));
        }
    }
}

void on_stage_all_clicked(AppState* state) {
    append_output_line(state, TEXT("[执行] git add -A"));
    int result = git_add_all(state->repo_path);
    if (result == 0) {
        append_output_line(state, TEXT("[成功] 暂存全部成功"));
        refresh_file_list(state);
    } else {
        append_output_line(state, TEXT("[错误] 暂存全部失败"));
    }
}

void on_select_repo_clicked(AppState* state) {
    IFileOpenDialog* pFileOpen = NULL;
    HRESULT hrCo = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    BOOL needUninitialize = (hrCo == S_OK);
    
    HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_ALL, 
                          &IID_IFileOpenDialog, (void**)&pFileOpen);
    
    if (SUCCEEDED(hr)) {
        DWORD options;
        pFileOpen->lpVtbl->GetOptions(pFileOpen, &options);
        pFileOpen->lpVtbl->SetOptions(pFileOpen, options | FOS_PICKFOLDERS);
        
        hr = pFileOpen->lpVtbl->Show(pFileOpen, state->hMainWnd);
    }
    
    if (SUCCEEDED(hr)) {
        IShellItem* pItem = NULL;
        hr = pFileOpen->lpVtbl->GetResult(pFileOpen, &pItem);
        
        if (SUCCEEDED(hr) && pItem) {
            PWSTR pszFilePath = NULL;
            hr = pItem->lpVtbl->GetDisplayName(pItem, SIGDN_FILESYSPATH, &pszFilePath);
            
            if (SUCCEEDED(hr) && pszFilePath) {
                char pathA[MAX_PATH_LEN];
                wide_to_utf8(pszFilePath, pathA, MAX_PATH_LEN);
                
                if (git_is_repository(pathA)) {
                    strncpy(state->repo_path, pathA, MAX_PATH_LEN - 1);
                    state->repo_path[MAX_PATH_LEN - 1] = '\0';
                    clear_output(state);
                    config_save_repo(state);
                    refresh_branch_info(state);
                    TabCtrl_SetCurSel(state->hTabControl, 1);
                    on_tab_changed(state, 1);
                } else {
                    append_output_line(state, TEXT("[错误] 所选目录不是 Git 仓库"));
                }
                
                CoTaskMemFree(pszFilePath);
            }
            pItem->lpVtbl->Release(pItem);
        }
    }
    
    if (pFileOpen) {
        pFileOpen->lpVtbl->Release(pFileOpen);
    }
    
    if (needUninitialize) {
        CoUninitialize();
    }
}

void on_tab_changed(AppState* state, int tabIndex) {
    state->currentTab = tabIndex;

    BOOL showStatus = (tabIndex == 0);
    BOOL showLog = (tabIndex == 1);

    if (state->hFileList) {
        ShowWindow(state->hFileList, showStatus ? SW_SHOW : SW_HIDE);
    }
    if (state->hLabelCommit) {
        ShowWindow(state->hLabelCommit, showStatus ? SW_SHOW : SW_HIDE);
    }
    if (state->hCommitMsg) {
        ShowWindow(state->hCommitMsg, showStatus ? SW_SHOW : SW_HIDE);
    }
    if (state->hBtnStage) {
        ShowWindow(state->hBtnStage, showStatus ? SW_SHOW : SW_HIDE);
    }
    if (state->hBtnStageAll) {
        ShowWindow(state->hBtnStageAll, showStatus ? SW_SHOW : SW_HIDE);
    }
    if (state->hBtnCommitNow) {
        ShowWindow(state->hBtnCommitNow, showStatus ? SW_SHOW : SW_HIDE);
    }
    if (state->hLabelOutput) {
        ShowWindow(state->hLabelOutput, showStatus ? SW_SHOW : SW_HIDE);
    }
    if (state->hOutput) {
        ShowWindow(state->hOutput, showStatus ? SW_SHOW : SW_HIDE);
    }

    if (state->hLogList) {
        ShowWindow(state->hLogList, showLog ? SW_SHOW : SW_HIDE);
    }
    if (state->hCommitFileList) {
        ShowWindow(state->hCommitFileList, showLog ? SW_SHOW : SW_HIDE);
    }

    if (showStatus) {
        refresh_file_list(state);
    } else if (showLog) {
        refresh_log_list(state);
    }
}

void on_file_double_click(AppState* state) {
    if (!state->hFileList) return;

    int selectedIndex = ListView_GetNextItem(state->hFileList, -1, LVNI_SELECTED);
    if (selectedIndex == -1) return;

    LVITEM lvi;
    memset(&lvi, 0, sizeof(lvi));
    lvi.mask = LVIF_PARAM;
    lvi.iItem = selectedIndex;
    ListView_GetItem(state->hFileList, &lvi);

    FileInfo* info = (FileInfo*)lvi.lParam;
    if (!info) return;

    TCHAR tempPath[MAX_PATH_LEN];
    TCHAR tempFileHead[MAX_PATH_LEN];
    TCHAR tempFileStaged[MAX_PATH_LEN];
    TCHAR currentFile[MAX_PATH_LEN];
    TCHAR bcomparePath[MAX_PATH_LEN];
    TCHAR cmdLine[MAX_CMD_LEN * 2];
    
    utf8_to_wide(state->bcompare_path, bcomparePath, MAX_PATH_LEN);
    
    utf8_to_wide(state->repo_path, tempPath, MAX_PATH_LEN);
    _sntprintf_s(currentFile, MAX_PATH_LEN, _TRUNCATE, TEXT("%s\\%S"), tempPath, info->path);
    
    GetTempPath(MAX_PATH_LEN, tempPath);
    GetTempFileName(tempPath, TEXT("git_head"), 0, tempFileHead);
    GetTempFileName(tempPath, TEXT("git_staged"), 0, tempFileStaged);

    char* headContent = NULL;
    char* stagedContent = NULL;
    int hasHeadVersion = 0;
    int hasStagedVersion = 0;

    if (info->status != FILE_STATUS_UNTRACKED && info->status != FILE_STATUS_ADDED) {
        headContent = (char*)malloc(MAX_OUTPUT_SIZE);
        if (headContent) {
            memset(headContent, 0, MAX_OUTPUT_SIZE);
            if (git_get_file_head_version(state->repo_path, info->path, headContent, MAX_OUTPUT_SIZE) == 0) {
                hasHeadVersion = 1;
                HANDLE hFile = CreateFile(tempFileHead, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hFile != INVALID_HANDLE_VALUE) {
                    DWORD written;
                    WriteFile(hFile, headContent, (DWORD)strlen(headContent), &written, NULL);
                    CloseHandle(hFile);
                }
            }
            free(headContent);
        }
    }

    if (info->staged && info->status != FILE_STATUS_UNTRACKED) {
        stagedContent = (char*)malloc(MAX_OUTPUT_SIZE);
        if (stagedContent) {
            memset(stagedContent, 0, MAX_OUTPUT_SIZE);
            if (git_get_file_staged_version(state->repo_path, info->path, stagedContent, MAX_OUTPUT_SIZE) == 0) {
                hasStagedVersion = 1;
                HANDLE hFile = CreateFile(tempFileStaged, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hFile != INVALID_HANDLE_VALUE) {
                    DWORD written;
                    WriteFile(hFile, stagedContent, (DWORD)strlen(stagedContent), &written, NULL);
                    CloseHandle(hFile);
                }
            }
            free(stagedContent);
        }
    }

    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    BOOL launched = FALSE;

    if (info->status == FILE_STATUS_UNTRACKED) {
        MessageBox(state->hMainWnd, TEXT("未跟踪文件没有旧版本可比较"), TEXT("提示"), MB_ICONINFORMATION);
        DeleteFile(tempFileHead);
        DeleteFile(tempFileStaged);
        return;
    }
    
    if (info->status == FILE_STATUS_DELETED) {
        if (hasHeadVersion) {
            _sntprintf_s(cmdLine, MAX_CMD_LEN * 2, _TRUNCATE, 
                TEXT("\"%s\" \"%s\" \"%s\""), bcomparePath, tempFileHead, currentFile);
            launched = CreateProcess(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
        }
    } else if (info->staged && hasStagedVersion) {
        if (hasHeadVersion) {
            _sntprintf_s(cmdLine, MAX_CMD_LEN * 2, _TRUNCATE, 
                TEXT("\"%s\" \"%s\" \"%s\""), bcomparePath, tempFileStaged, currentFile);
            launched = CreateProcess(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
        }
    } else if (hasHeadVersion) {
        _sntprintf_s(cmdLine, MAX_CMD_LEN * 2, _TRUNCATE, 
            TEXT("\"%s\" \"%s\" \"%s\""), bcomparePath, tempFileHead, currentFile);
        launched = CreateProcess(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    }

    if (launched) {
        // 注册临时文件，程序退出时清理
        register_temp_file(tempFileHead);
        register_temp_file(tempFileStaged);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        MessageBox(state->hMainWnd, TEXT("无法启动 Beyond Compare 或没有可比较的版本"), TEXT("错误"), MB_ICONERROR);
        DeleteFile(tempFileHead);
        DeleteFile(tempFileStaged);
    }
}

void on_log_double_click(AppState* state) {
    if (!state->hLogList) return;

    int selectedIndex = ListView_GetNextItem(state->hLogList, -1, LVNI_SELECTED);
    if (selectedIndex == -1) return;

    LVITEM lvi;
    memset(&lvi, 0, sizeof(lvi));
    lvi.mask = LVIF_PARAM;
    lvi.iItem = selectedIndex;
    ListView_GetItem(state->hLogList, &lvi);

    CommitInfo* info = (CommitInfo*)lvi.lParam;
    if (!info) return;

    strncpy(state->selected_commit_hash, info->hash, sizeof(state->selected_commit_hash) - 1);
    state->selected_commit_hash[sizeof(state->selected_commit_hash) - 1] = '\0';

    if (state->hCommitFileList) {
        // 先清除 lParam，避免悬空指针
        int itemCount = ListView_GetItemCount(state->hCommitFileList);
        for (int i = 0; i < itemCount; i++) {
            LVITEM lvi2;
            lvi2.mask = LVIF_PARAM;
            lvi2.iItem = i;
            lvi2.lParam = 0;
            ListView_SetItem(state->hCommitFileList, &lvi2);
        }
        ListView_DeleteAllItems(state->hCommitFileList);
    }

    if (state->commit_file_list) {
        git_free_commit_file_list(state->commit_file_list);
        state->commit_file_list = NULL;
    }

    state->commit_file_list = git_get_commit_files(state->repo_path, info->hash);
    ShowWindow(state->hCommitFileList, SW_SHOW);

    CommitFileInfo* fileInfo = state->commit_file_list;
    int index = 0;
    while (fileInfo) {
        TCHAR wpath[MAX_PATH_LEN];
        TCHAR wstatus[16];

        utf8_to_wide(fileInfo->path, wpath, MAX_PATH_LEN);
        utf8_to_wide(fileInfo->status, wstatus, 16);

        LVITEM lvi2;
        memset(&lvi2, 0, sizeof(lvi2));
        lvi2.mask = LVIF_TEXT | LVIF_PARAM;
        lvi2.iItem = index;
        lvi2.lParam = (LPARAM)fileInfo;
        lvi2.pszText = wpath;
        ListView_InsertItem(state->hCommitFileList, &lvi2);

        lvi2.mask = LVIF_TEXT;
        lvi2.iSubItem = 1;
        lvi2.pszText = wstatus;
        ListView_SetItem(state->hCommitFileList, &lvi2);

        fileInfo = fileInfo->next;
        index++;
    }

    ListView_SetColumnWidth(state->hCommitFileList, 0, LVSCW_AUTOSIZE);
    ListView_SetColumnWidth(state->hCommitFileList, 1, 60);
}

void on_commit_file_double_click(AppState* state) {
    if (!state->hCommitFileList || !state->selected_commit_hash[0]) return;

    int selectedIndex = ListView_GetNextItem(state->hCommitFileList, -1, LVNI_SELECTED);
    if (selectedIndex == -1) return;

    LVITEM lvi;
    memset(&lvi, 0, sizeof(lvi));
    lvi.mask = LVIF_PARAM;
    lvi.iItem = selectedIndex;
    ListView_GetItem(state->hCommitFileList, &lvi);

    CommitFileInfo* info = (CommitFileInfo*)lvi.lParam;
    if (!info) return;

    TCHAR tempPath[MAX_PATH_LEN];
    TCHAR tempFileOld[MAX_PATH_LEN];
    TCHAR tempFileNew[MAX_PATH_LEN];
    TCHAR bcomparePath[MAX_PATH_LEN];
    TCHAR cmdLine[MAX_CMD_LEN * 2];

    utf8_to_wide(state->bcompare_path, bcomparePath, MAX_PATH_LEN);

    GetTempPath(MAX_PATH_LEN, tempPath);
    GetTempFileName(tempPath, TEXT("git_old"), 0, tempFileOld);
    GetTempFileName(tempPath, TEXT("git_new"), 0, tempFileNew);

    char* oldContent = (char*)malloc(MAX_OUTPUT_SIZE);
    char* newContent = (char*)malloc(MAX_OUTPUT_SIZE);

    int hasOldVersion = 0;
    int hasNewVersion = 0;

    char prevHash[32];
    snprintf(prevHash, sizeof(prevHash), "%s^", state->selected_commit_hash);

    if (oldContent) {
        memset(oldContent, 0, MAX_OUTPUT_SIZE);
        if (git_get_file_version(state->repo_path, prevHash, info->path, oldContent, MAX_OUTPUT_SIZE) == 0) {
            hasOldVersion = 1;
            HANDLE hFile = CreateFile(tempFileOld, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                DWORD written;
                WriteFile(hFile, oldContent, (DWORD)strlen(oldContent), &written, NULL);
                CloseHandle(hFile);
            }
        }
        free(oldContent);
    }

    if (newContent && strcmp(info->status, "D") != 0) {
        memset(newContent, 0, MAX_OUTPUT_SIZE);
        if (git_get_file_version(state->repo_path, state->selected_commit_hash, info->path, newContent, MAX_OUTPUT_SIZE) == 0) {
            hasNewVersion = 1;
            HANDLE hFile = CreateFile(tempFileNew, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                DWORD written;
                WriteFile(hFile, newContent, (DWORD)strlen(newContent), &written, NULL);
                CloseHandle(hFile);
            }
        }
        free(newContent);
    }

    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    BOOL launched = FALSE;

    if (strcmp(info->status, "A") == 0) {
        if (hasNewVersion) {
            _sntprintf_s(cmdLine, MAX_CMD_LEN * 2, _TRUNCATE,
                TEXT("\"%s\" \"%s\""), bcomparePath, tempFileNew);
            launched = CreateProcess(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
        }
    } else if (strcmp(info->status, "D") == 0) {
        if (hasOldVersion) {
            _sntprintf_s(cmdLine, MAX_CMD_LEN * 2, _TRUNCATE,
                TEXT("\"%s\" \"%s\""), bcomparePath, tempFileOld);
            launched = CreateProcess(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
        }
    } else {
        if (hasOldVersion && hasNewVersion) {
            _sntprintf_s(cmdLine, MAX_CMD_LEN * 2, _TRUNCATE,
                TEXT("\"%s\" \"%s\" \"%s\""), bcomparePath, tempFileOld, tempFileNew);
            launched = CreateProcess(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
        }
    }

    if (launched) {
        // 注册临时文件，程序退出时清理
        register_temp_file(tempFileOld);
        register_temp_file(tempFileNew);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        MessageBox(state->hMainWnd, TEXT("无法启动 Beyond Compare"), TEXT("错误"), MB_ICONERROR);
        DeleteFile(tempFileOld);
        DeleteFile(tempFileNew);
    }
}

static char g_ini_path[MAX_PATH_LEN];

static void get_ini_path(void) {
    GetModuleFileNameA(NULL, g_ini_path, MAX_PATH_LEN);
    char* ext = strrchr(g_ini_path, '.');
    if (ext) *ext = '\0';
    strcat(g_ini_path, ".ini");
}

void config_load(AppState* state) {
    get_ini_path();
    GetPrivateProfileStringA("Settings", "GitPath", "git.exe", state->git_path, MAX_PATH_LEN, g_ini_path);
    GetPrivateProfileStringA("Settings", "BComparePath", "", state->bcompare_path, MAX_PATH_LEN, g_ini_path);
    GetPrivateProfileStringA("Settings", "LastRepo", ".", state->repo_path, MAX_PATH_LEN, g_ini_path);
}

void config_save(AppState* state) {
    get_ini_path();
    WritePrivateProfileStringA("Settings", "GitPath", state->git_path, g_ini_path);
    WritePrivateProfileStringA("Settings", "BComparePath", state->bcompare_path, g_ini_path);
    WritePrivateProfileStringA("Settings", "LastRepo", state->repo_path, g_ini_path);
}

void config_save_repo(AppState* state) {
    get_ini_path();
    WritePrivateProfileStringA("Settings", "LastRepo", state->repo_path, g_ini_path);
}

typedef struct SettingsCtx {
    AppState* state;
    HWND hGitEdit;
    HWND hBCompareEdit;
    HWND hDlg;
    HFONT hFont;
} SettingsCtx;

static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsCtx* ctx = (SettingsCtx*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    switch (message) {
        case WM_CREATE: {
            LPCREATESTRUCT cs = (LPCREATESTRUCT)lParam;
            ctx = (SettingsCtx*)cs->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)ctx);
            ctx->hDlg = hWnd;
            ctx->hFont = CreateFont(
                -MulDiv(9, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72),
                0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                TEXT("Microsoft YaHei")
            );
            int y = 15;
            CreateWindowEx(0, TEXT("STATIC"), TEXT("Git 路径:"),
                WS_CHILD | WS_VISIBLE, 15, y, 60, 20, hWnd, NULL, NULL, NULL);
            ctx->hGitEdit = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 80, y, 300, 22, hWnd, NULL, NULL, NULL);
            CreateWindowEx(0, TEXT("BUTTON"), TEXT("浏览..."),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 390, y, 60, 22, hWnd, (HMENU)ID_SETTINGS_BROWSE_GIT, NULL, NULL);
            y += 35;
            CreateWindowEx(0, TEXT("STATIC"), TEXT("Beyond Compare 路径:"),
                WS_CHILD | WS_VISIBLE, 15, y, 140, 20, hWnd, NULL, NULL, NULL);
            ctx->hBCompareEdit = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), TEXT(""),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 155, y, 225, 22, hWnd, NULL, NULL, NULL);
            CreateWindowEx(0, TEXT("BUTTON"), TEXT("浏览..."),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 390, y, 60, 22, hWnd, (HMENU)ID_SETTINGS_BROWSE_BCOMPARE, NULL, NULL);
            y += 40;
            int btnW = 80, btnH = 28, btnGap = 10;
            int btnRowW = btnW * 2 + btnGap;
            int btnX = (480 - btnRowW) / 2;
            CreateWindowEx(0, TEXT("BUTTON"), TEXT("保存"),
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, btnX, y, btnW, btnH, hWnd, (HMENU)ID_SETTINGS_SAVE, NULL, NULL);
            CreateWindowEx(0, TEXT("BUTTON"), TEXT("取消"),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, btnX + btnW + btnGap, y, btnW, btnH, hWnd, (HMENU)ID_SETTINGS_CANCEL, NULL, NULL);
            HWND hChild = GetWindow(hWnd, GW_CHILD);
            while (hChild) {
                SendMessage(hChild, WM_SETFONT, (WPARAM)ctx->hFont, FALSE);
                hChild = GetWindow(hChild, GW_HWNDNEXT);
            }
            TCHAR wgit[MAX_PATH_LEN];
            TCHAR wbcompare[MAX_PATH_LEN];
            utf8_to_wide(ctx->state->git_path, wgit, MAX_PATH_LEN);
            utf8_to_wide(ctx->state->bcompare_path, wbcompare, MAX_PATH_LEN);
            SetWindowText(ctx->hGitEdit, wgit);
            SetWindowText(ctx->hBCompareEdit, wbcompare);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_SETTINGS_BROWSE_GIT: {
                    OPENFILENAME ofn;
                    TCHAR szFile[MAX_PATH_LEN] = {0};
                    memset(&ofn, 0, sizeof(ofn));
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hWnd;
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = MAX_PATH_LEN;
                    ofn.lpstrFilter = TEXT("Executable\0*.exe\0All\0*.*\0");
                    ofn.nFilterIndex = 1;
                    ofn.lpstrTitle = TEXT("选择 git.exe");
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                    if (GetOpenFileName(&ofn)) {
                        SetWindowText(ctx->hGitEdit, szFile);
                    }
                    break;
                }
                case ID_SETTINGS_BROWSE_BCOMPARE: {
                    OPENFILENAME ofn;
                    TCHAR szFile[MAX_PATH_LEN] = {0};
                    memset(&ofn, 0, sizeof(ofn));
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hWnd;
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = MAX_PATH_LEN;
                    ofn.lpstrFilter = TEXT("Executable\0*.exe\0All\0*.*\0");
                    ofn.nFilterIndex = 1;
                    ofn.lpstrTitle = TEXT("选择 BCompare.exe");
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                    if (GetOpenFileName(&ofn)) {
                        SetWindowText(ctx->hBCompareEdit, szFile);
                    }
                    break;
                }
                case ID_SETTINGS_SAVE: {
                    TCHAR buf[MAX_PATH_LEN];
                    GetWindowText(ctx->hGitEdit, buf, MAX_PATH_LEN);
                    wide_to_utf8(buf, ctx->state->git_path, MAX_PATH_LEN);
                    GetWindowText(ctx->hBCompareEdit, buf, MAX_PATH_LEN);
                    wide_to_utf8(buf, ctx->state->bcompare_path, MAX_PATH_LEN);
                    config_save(ctx->state);
                    DestroyWindow(hWnd);
                    break;
                }
                case ID_SETTINGS_CANCEL:
                    DestroyWindow(hWnd);
                    break;
            }
            break;
        case WM_DESTROY:
            if (ctx && ctx->hFont) DeleteObject(ctx->hFont);
            if (ctx) free(ctx);
            break;
        case WM_CLOSE:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void on_settings_clicked(AppState* state) {
    static WNDCLASSEX wcex = {0};
    if (!wcex.cbSize) {
        wcex.cbSize = sizeof(WNDCLASSEX);
        wcex.lpfnWndProc = SettingsWndProc;
        wcex.hInstance = GetModuleHandle(NULL);
        wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wcex.lpszClassName = TEXT("GitGUISettingsClass");
        wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassEx(&wcex);
    }
    SettingsCtx* ctx = (SettingsCtx*)malloc(sizeof(SettingsCtx));
    ctx->state = state;
    ctx->hGitEdit = NULL;
    ctx->hBCompareEdit = NULL;
    ctx->hDlg = NULL;
    ctx->hFont = NULL;
    int dlgW = 480, dlgH = 150;
    RECT rc = { 0, 0, dlgW, dlgH };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;
    RECT mainRc;
    GetWindowRect(state->hMainWnd, &mainRc);
    int x = mainRc.left + (mainRc.right - mainRc.left - winW) / 2;
    int y = mainRc.top + (mainRc.bottom - mainRc.top - winH) / 2;
    CreateWindowEx(WS_EX_DLGMODALFRAME, TEXT("GitGUISettingsClass"),
        TEXT("设置"), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, winW, winH, state->hMainWnd, NULL, GetModuleHandle(NULL), ctx);
}

void on_file_list_context_menu(AppState* state, POINT pt) {
    if (!state->hFileList) return;

    int selectedIndex = ListView_GetNextItem(state->hFileList, -1, LVNI_SELECTED);
    if (selectedIndex == -1) return;

    LVITEM lvi;
    memset(&lvi, 0, sizeof(lvi));
    lvi.mask = LVIF_PARAM;
    lvi.iItem = selectedIndex;
    ListView_GetItem(state->hFileList, &lvi);

    FileInfo* info = (FileInfo*)lvi.lParam;
    if (!info) return;

    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, IDM_FILE_VIEW_DIFF, TEXT("查看改动"));
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);

    if (info->status != FILE_STATUS_UNTRACKED) {
        AppendMenu(hMenu, MF_STRING, IDM_FILE_REVERT, TEXT("回退文件改动"));
    }
    if (info->staged) {
        AppendMenu(hMenu, MF_STRING, IDM_FILE_UNSTAGE, TEXT("取消暂存"));
    } else if (info->status != FILE_STATUS_UNTRACKED) {
        AppendMenu(hMenu, MF_STRING, IDM_FILE_STAGE, TEXT("暂存文件"));
    }
    AppendMenu(hMenu, MF_STRING, IDM_FILE_DELETE, TEXT("删除文件"));

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        pt.x, pt.y, 0, state->hFileList, NULL);
    DestroyMenu(hMenu);

    if (cmd == 0) return;

    char cmdLine[MAX_CMD_LEN];
    char output[MAX_OUTPUT_SIZE];

    switch (cmd) {
        case IDM_FILE_VIEW_DIFF:
            on_file_double_click(state);
            break;

        case IDM_FILE_REVERT: {
            if (MessageBox(state->hMainWnd, TEXT("确定要回退该文件的改动吗？"), TEXT("确认回退"), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                snprintf(cmdLine, MAX_CMD_LEN, "checkout -- \"%s\"", info->path);
                append_output_line(state, TEXT("[回退] 正在回退文件..."));
                if (git_execute(state->repo_path, cmdLine, output, MAX_OUTPUT_SIZE) == 0) {
                    append_output_line(state, TEXT("[成功] 文件已回退"));
                    refresh_file_list(state);
                } else {
                    append_output_line(state, TEXT("[错误] 回退失败"));
                }
            }
            break;
        }

        case IDM_FILE_STAGE: {
            snprintf(cmdLine, MAX_CMD_LEN, "add \"%s\"", info->path);
            append_output_line(state, TEXT("[暂存] 正在暂存文件..."));
            if (git_execute(state->repo_path, cmdLine, output, MAX_OUTPUT_SIZE) == 0) {
                append_output_line(state, TEXT("[成功] 文件已暂存"));
                refresh_file_list(state);
            } else {
                append_output_line(state, TEXT("[错误] 暂存失败"));
            }
            break;
        }

        case IDM_FILE_UNSTAGE: {
            snprintf(cmdLine, MAX_CMD_LEN, "reset HEAD -- \"%s\"", info->path);
            append_output_line(state, TEXT("[取消暂存] 正在取消暂存..."));
            if (git_execute(state->repo_path, cmdLine, output, MAX_OUTPUT_SIZE) == 0) {
                append_output_line(state, TEXT("[成功] 已取消暂存"));
                refresh_file_list(state);
            } else {
                append_output_line(state, TEXT("[错误] 取消暂存失败"));
            }
            break;
        }

        case IDM_FILE_DELETE: {
            TCHAR msgW[MAX_PATH_LEN];
            TCHAR wpath[MAX_PATH_LEN];
            utf8_to_wide(info->path, wpath, MAX_PATH_LEN);
            _sntprintf_s(msgW, MAX_PATH_LEN, _TRUNCATE, TEXT("确定要删除文件 \"%s\" 吗？\n此操作不可撤销！"), wpath);
            if (MessageBox(state->hMainWnd, msgW, TEXT("确认删除"), MB_YESNO | MB_ICONWARNING) == IDYES) {
                char cleanPath[MAX_PATH_LEN];
                strncpy(cleanPath, info->path, MAX_PATH_LEN - 1);
                cleanPath[MAX_PATH_LEN - 1] = '\0';
                int len = (int)strlen(cleanPath);
                if (len >= 2 && cleanPath[0] == '"' && cleanPath[len - 1] == '"') {
                    cleanPath[len - 1] = '\0';
                    memmove(cleanPath, cleanPath + 1, len - 1);
                }
                char absPath[MAX_PATH_LEN];
                char fullPathA[MAX_PATH_LEN];
                if (GetFullPathNameA(state->repo_path, MAX_PATH_LEN, absPath, NULL) == 0) {
                    strncpy(absPath, state->repo_path, MAX_PATH_LEN - 1);
                }
                snprintf(fullPathA, MAX_PATH_LEN, "%s/%s", absPath, cleanPath);
                TCHAR wfull[MAX_PATH_LEN];
                utf8_to_wide(fullPathA, wfull, MAX_PATH_LEN);
                SetFileAttributes(wfull, FILE_ATTRIBUTE_NORMAL);
                if (DeleteFile(wfull)) {
                    append_output_line(state, TEXT("[成功] 文件已删除"));
                    refresh_file_list(state);
                } else {
                    TCHAR errMsg[512];
                    DWORD err = GetLastError();
                    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, 0, errMsg, 512, NULL);
                    TCHAR fullMsg[1024];
                    _sntprintf_s(fullMsg, 1024, _TRUNCATE, TEXT("删除失败 (错误码 %lu):\n%s\n路径: %s"), err, errMsg, wfull);
                    MessageBox(state->hMainWnd, fullMsg, TEXT("错误"), MB_ICONERROR);
                }
            }
            break;
        }
    }
}

void on_log_list_context_menu(AppState* state, POINT pt) {
    if (!state->hLogList) return;

    int selectedIndex = ListView_GetNextItem(state->hLogList, -1, LVNI_SELECTED);
    if (selectedIndex == -1) return;

    LVITEM lvi;
    memset(&lvi, 0, sizeof(lvi));
    lvi.mask = LVIF_PARAM;
    lvi.iItem = selectedIndex;
    ListView_GetItem(state->hLogList, &lvi);

    CommitInfo* info = (CommitInfo*)lvi.lParam;
    if (!info) return;

    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, IDM_LOG_RESET_HARD, TEXT("回退到此版本"));
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, IDM_LOG_NEW_BRANCH, TEXT("从此版本创建分支"));
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, IDM_LOG_VIEW_REFLOG, TEXT("查看操作记录"));
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, IDM_LOG_COPY_HASH, TEXT("复制哈希值"));

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        pt.x, pt.y, 0, state->hLogList, NULL);
    DestroyMenu(hMenu);

    if (cmd == 0) return;

    char cmdLine[MAX_CMD_LEN];
    char output[MAX_OUTPUT_SIZE];

    switch (cmd) {
        case IDM_LOG_RESET_HARD: {
            TCHAR msg[MAX_PATH_LEN];
            TCHAR whash[32];
            utf8_to_wide(info->hash, whash, 32);
            _sntprintf_s(msg, MAX_PATH_LEN, _TRUNCATE,
                TEXT("确定要回退到版本 %s 吗？\n此操作将丢弃之后的所有提交，且不可撤销！"), whash);
            if (MessageBox(state->hMainWnd, msg, TEXT("确认回退"), MB_YESNO | MB_ICONWARNING) == IDYES) {
                snprintf(cmdLine, MAX_CMD_LEN, "reset --hard %s", info->hash);
                append_output_line(state, TEXT("[回退] 正在回退到指定版本..."));
                if (git_execute(state->repo_path, cmdLine, output, MAX_OUTPUT_SIZE) == 0) {
                    append_output_line(state, TEXT("[成功] 已回退到指定版本"));
                    refresh_file_list(state);
                    refresh_log_list(state);
                } else {
                    append_output_line(state, TEXT("[错误] 回退失败"));
                }
            }
            break;
        }

        case IDM_LOG_NEW_BRANCH: {
            TCHAR branchName[256];
            if (InputBox(state->hMainWnd, TEXT("输入新分支名称"), TEXT("创建新分支"), branchName, 256)) {
                char branchA[256];
                wide_to_utf8(branchName, branchA, 256);
                snprintf(cmdLine, MAX_CMD_LEN, "checkout -b \"%s\" %s", branchA, info->hash);
                append_output_line(state, TEXT("[分支] 正在创建新分支..."));
                if (git_execute(state->repo_path, cmdLine, output, MAX_OUTPUT_SIZE) == 0) {
                    append_output_line(state, TEXT("[成功] 已创建并切换到新分支"));
                    refresh_branch_info(state);
                    refresh_file_list(state);
                    refresh_log_list(state);
                } else {
                    append_output_line(state, TEXT("[错误] 创建分支失败"));
                }
            }
            break;
        }

        case IDM_LOG_VIEW_REFLOG: {
            char* reflogOutput = (char*)malloc(MAX_OUTPUT_SIZE);
            if (!reflogOutput) break;
            memset(reflogOutput, 0, MAX_OUTPUT_SIZE);
            append_output_line(state, TEXT("[操作记录] 正在加载 reflog..."));
            if (git_execute(state->repo_path, "reflog --pretty=format:\"%h  %gs\"", reflogOutput, MAX_OUTPUT_SIZE) == 0) {
                char* line = strtok(reflogOutput, "\n");
                while (line) {
                    TCHAR wline[1024];
                    utf8_to_wide(line, wline, 1024);
                    append_output_line(state, wline);
                    line = strtok(NULL, "\n");
                }
            } else {
                append_output_line(state, TEXT("[错误] 加载 reflog 失败"));
            }
            free(reflogOutput);
            TabCtrl_SetCurSel(state->hTabControl, 0);
            on_tab_changed(state, 0);
            break;
        }

        case IDM_LOG_COPY_HASH: {
            if (OpenClipboard(state->hMainWnd)) {
                EmptyClipboard();
                SIZE_T len = strlen(info->hash) + 1;
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len * sizeof(TCHAR));
                if (hMem) {
                    TCHAR* pMem = (TCHAR*)GlobalLock(hMem);
                    if (pMem) {
                        utf8_to_wide(info->hash, pMem, (int)len);
                        GlobalUnlock(hMem);
                        SetClipboardData(CF_UNICODETEXT, hMem);
                    } else {
                        GlobalFree(hMem);
                    }
                }
                CloseClipboard();
            }
            break;
        }
    }
}
