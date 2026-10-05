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
#define MAX_TEMP_FILES 256
static TCHAR g_tempFiles[MAX_TEMP_FILES][MAX_PATH_LEN];
static int g_tempFileCount = 0;

static void register_temp_file(const TCHAR* path) {
    // 去重，避免同一文件被重复登记
    for (int i = 0; i < g_tempFileCount; i++) {
        if (_tcscmp(g_tempFiles[i], path) == 0) return;
    }
    if (g_tempFileCount < MAX_TEMP_FILES) {
        _tcsncpy_s(g_tempFiles[g_tempFileCount], MAX_PATH_LEN, path, _TRUNCATE);
        g_tempFileCount++;
    } else {
        // 容量已满：立即删除该临时文件，避免在 %TEMP% 中泄漏
        DeleteFile(path);
    }
}

static void cleanup_temp_files(void) {
    for (int i = 0; i < g_tempFileCount; i++) {
        DeleteFile(g_tempFiles[i]);
    }
    g_tempFileCount = 0;
}

// 获取列表控件当前选中项关联的 lParam（无选中返回 0）
static LPARAM get_selected_lparam(HWND hList) {
    int idx = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
    if (idx == -1) return 0;

    LVITEM lvi;
    memset(&lvi, 0, sizeof(lvi));
    lvi.mask = LVIF_PARAM;
    lvi.iItem = idx;
    if (!ListView_GetItem(hList, &lvi)) return 0;
    return lvi.lParam;
}

// 将 UTF-8 内容写入指定文件；成功返回 TRUE
static BOOL write_file_utf8(const TCHAR* path, const char* content) {
    HANDLE hFile = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    DWORD written;
    BOOL ok = WriteFile(hFile, content, (DWORD)strlen(content), &written, NULL);
    CloseHandle(hFile);
    return ok;
}

// 使用 Beyond Compare 比较 1~3 个文件；成功启动返回 TRUE
static BOOL launch_compare(AppState* state, const TCHAR* f1, const TCHAR* f2, const TCHAR* f3) {
    TCHAR bcomparePath[MAX_PATH_LEN];
    TCHAR cmdLine[MAX_CMD_LEN * 2];
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    utf8_to_wide(state->bcompare_path, bcomparePath, MAX_PATH_LEN);

    if (f3) {
        _sntprintf_s(cmdLine, MAX_CMD_LEN * 2, _TRUNCATE,
            TEXT("\"%s\" \"%s\" \"%s\" \"%s\""), bcomparePath, f1, f2, f3);
    } else if (f2) {
        _sntprintf_s(cmdLine, MAX_CMD_LEN * 2, _TRUNCATE,
            TEXT("\"%s\" \"%s\" \"%s\""), bcomparePath, f1, f2);
    } else {
        _sntprintf_s(cmdLine, MAX_CMD_LEN * 2, _TRUNCATE,
            TEXT("\"%s\" \"%s\""), bcomparePath, f1);
    }

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcess(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        return FALSE;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return TRUE;
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
            // 注意：ctx->hFont 为全局共享字体（CreateUIFont 管理），此处不得删除
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
        wcex.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON));
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

// 将 UTF-8 文本追加到输出框（自动转换为宽字符）
static void append_output_utf8(AppState* state, const char* utf8) {
    if (!utf8 || !utf8[0]) return;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wlen <= 0) return;

    TCHAR* wbuf = (TCHAR*)malloc((size_t)wlen * sizeof(TCHAR));
    if (!wbuf) return;

    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wbuf, wlen);
    append_output(state, wbuf);
    free(wbuf);
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
    char* output = (char*)malloc(65536);
    if (!output) return;
    output[0] = '\0';

    append_output_line(state, TEXT("[执行] git push..."));
    int result = git_push(state->repo_path, output, 65536);

    append_output_utf8(state, output);
    free(output);

    if (result == 0) {
        append_output_line(state, TEXT("[成功] 推送成功"));
    } else {
        append_output_line(state, TEXT("[错误] 推送失败"));
    }

    refresh_log_list(state);
}

void on_pull_clicked(AppState* state) {
    char* output = (char*)malloc(65536);
    if (!output) return;
    output[0] = '\0';

    append_output_line(state, TEXT("[执行] git pull..."));
    int result = git_pull(state->repo_path, output, 65536);

    append_output_utf8(state, output);
    free(output);

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

    FileInfo* info = (FileInfo*)get_selected_lparam(state->hFileList);
    if (!info) {
        append_output_line(state, TEXT("[警告] 请先选择要暂存的文件"));
        return;
    }

    TCHAR wpath[MAX_PATH_LEN];
    utf8_to_wide(info->path, wpath, MAX_PATH_LEN);
    TCHAR msg[MAX_PATH_LEN + 50];
    _sntprintf_s(msg, MAX_PATH_LEN + 50, _TRUNCATE, TEXT("[执行] git add \"%s\""), wpath);
    append_output_line(state, msg);

    if (git_add(state->repo_path, info->path) == 0) {
        append_output_line(state, TEXT("[成功] 暂存成功"));
        refresh_file_list(state);
    } else {
        append_output_line(state, TEXT("[错误] 暂存失败"));
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

    FileInfo* info = (FileInfo*)get_selected_lparam(state->hFileList);
    if (!info) return;

    if (info->status == FILE_STATUS_UNTRACKED) {
        MessageBox(state->hMainWnd, TEXT("未跟踪文件没有旧版本可比较"), TEXT("提示"), MB_ICONINFORMATION);
        return;
    }

    TCHAR tempPath[MAX_PATH_LEN];
    TCHAR tempFileHead[MAX_PATH_LEN];
    TCHAR tempFileStaged[MAX_PATH_LEN];
    TCHAR currentFile[MAX_PATH_LEN];
    TCHAR wrel[MAX_PATH_LEN];

    // 工作区文件绝对路径：<仓库路径>\<相对路径>
    utf8_to_wide(state->repo_path, tempPath, MAX_PATH_LEN);
    utf8_to_wide(info->path, wrel, MAX_PATH_LEN);
    _sntprintf_s(currentFile, MAX_PATH_LEN, _TRUNCATE, TEXT("%s\\%s"), tempPath, wrel);

    GetTempPath(MAX_PATH_LEN, tempPath);
    GetTempFileName(tempPath, TEXT("git_head"), 0, tempFileHead);
    GetTempFileName(tempPath, TEXT("git_staged"), 0, tempFileStaged);

    int hasHeadVersion = 0;
    int hasStagedVersion = 0;
    char* content = NULL;

    // 新增文件没有 HEAD 版本
    if (info->status != FILE_STATUS_ADDED) {
        content = (char*)malloc(MAX_OUTPUT_SIZE);
        if (content) {
            memset(content, 0, MAX_OUTPUT_SIZE);
            if (git_get_file_head_version(state->repo_path, info->path, content, MAX_OUTPUT_SIZE) == 0) {
                hasHeadVersion = write_file_utf8(tempFileHead, content);
            }
            free(content);
        }
    }

    if (info->staged) {
        content = (char*)malloc(MAX_OUTPUT_SIZE);
        if (content) {
            memset(content, 0, MAX_OUTPUT_SIZE);
            if (git_get_file_staged_version(state->repo_path, info->path, content, MAX_OUTPUT_SIZE) == 0) {
                hasStagedVersion = write_file_utf8(tempFileStaged, content);
            }
            free(content);
        }
    }

    // 选择“旧版本”：已暂存文件与暂存版本比较，其余与 HEAD 比较
    const TCHAR* oldFile = NULL;
    if (info->status == FILE_STATUS_DELETED) {
        if (hasHeadVersion) oldFile = tempFileHead;
    } else if (info->staged && hasStagedVersion) {
        oldFile = tempFileStaged;
    } else if (hasHeadVersion) {
        oldFile = tempFileHead;
    }

    if (oldFile && launch_compare(state, oldFile, currentFile, NULL)) {
        register_temp_file(tempFileHead);
        register_temp_file(tempFileStaged);
    } else {
        MessageBox(state->hMainWnd, TEXT("无法启动 Beyond Compare 或没有可比较的版本"), TEXT("错误"), MB_ICONERROR);
        DeleteFile(tempFileHead);
        DeleteFile(tempFileStaged);
    }
}

static void populate_commit_file_list(AppState* state, const char* commit_hash) {
    if (!state->hCommitFileList) return;

    ListView_DeleteAllItems(state->hCommitFileList);

    if (state->commit_file_list) {
        git_free_commit_file_list(state->commit_file_list);
        state->commit_file_list = NULL;
    }

    state->commit_file_list = git_get_commit_files(state->repo_path, commit_hash);

    CommitFileInfo* fileInfo = state->commit_file_list;
    int index = 0;
    while (fileInfo) {
        TCHAR wpath[MAX_PATH_LEN];
        TCHAR wstatus[16];

        utf8_to_wide(fileInfo->path, wpath, MAX_PATH_LEN);
        utf8_to_wide(fileInfo->status, wstatus, 16);

        LVITEM lvi;
        memset(&lvi, 0, sizeof(lvi));
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = index;
        lvi.lParam = (LPARAM)fileInfo;
        lvi.pszText = wpath;
        ListView_InsertItem(state->hCommitFileList, &lvi);

        lvi.mask = LVIF_TEXT;
        lvi.iSubItem = 1;
        lvi.pszText = wstatus;
        ListView_SetItem(state->hCommitFileList, &lvi);

        fileInfo = fileInfo->next;
        index++;
    }

    ListView_SetColumnWidth(state->hCommitFileList, 0, LVSCW_AUTOSIZE);
    ListView_SetColumnWidth(state->hCommitFileList, 1, 60);
}

void on_log_double_click(AppState* state) {
    if (!state->hLogList) return;

    CommitInfo* info = (CommitInfo*)get_selected_lparam(state->hLogList);
    if (!info) return;

    strncpy(state->selected_commit_hash, info->hash, sizeof(state->selected_commit_hash) - 1);
    state->selected_commit_hash[sizeof(state->selected_commit_hash) - 1] = '\0';

    populate_commit_file_list(state, info->hash);

    if (state->hCommitFileList) {
        ShowWindow(state->hCommitFileList, SW_SHOW);
    }
}

void on_commit_file_double_click(AppState* state) {
    if (!state->hCommitFileList || !state->selected_commit_hash[0]) return;

    CommitFileInfo* info = (CommitFileInfo*)get_selected_lparam(state->hCommitFileList);
    if (!info) return;

    TCHAR tempPath[MAX_PATH_LEN];
    TCHAR tempFileOld[MAX_PATH_LEN];
    TCHAR tempFileNew[MAX_PATH_LEN];

    GetTempPath(MAX_PATH_LEN, tempPath);
    GetTempFileName(tempPath, TEXT("git_old"), 0, tempFileOld);
    GetTempFileName(tempPath, TEXT("git_new"), 0, tempFileNew);

    int hasOldVersion = 0;
    int hasNewVersion = 0;
    char* content = NULL;

    char prevHash[64];
    snprintf(prevHash, sizeof(prevHash), "%s^", state->selected_commit_hash);

    content = (char*)malloc(MAX_OUTPUT_SIZE);
    if (content) {
        memset(content, 0, MAX_OUTPUT_SIZE);
        if (git_get_file_version(state->repo_path, prevHash, info->path, content, MAX_OUTPUT_SIZE) == 0) {
            hasOldVersion = write_file_utf8(tempFileOld, content);
        }
        free(content);
    }

    // 删除类型的提交文件没有新版本内容
    if (strcmp(info->status, "D") != 0) {
        content = (char*)malloc(MAX_OUTPUT_SIZE);
        if (content) {
            memset(content, 0, MAX_OUTPUT_SIZE);
            if (git_get_file_version(state->repo_path, state->selected_commit_hash, info->path, content, MAX_OUTPUT_SIZE) == 0) {
                hasNewVersion = write_file_utf8(tempFileNew, content);
            }
            free(content);
        }
    }

    BOOL launched = FALSE;

    if (strcmp(info->status, "A") == 0) {
        if (hasNewVersion) launched = launch_compare(state, tempFileNew, NULL, NULL);
    } else if (strcmp(info->status, "D") == 0) {
        if (hasOldVersion) launched = launch_compare(state, tempFileOld, NULL, NULL);
    } else {
        if (hasOldVersion && hasNewVersion) launched = launch_compare(state, tempFileOld, tempFileNew, NULL);
    }

    if (launched) {
        register_temp_file(tempFileOld);
        register_temp_file(tempFileNew);
    } else {
        MessageBox(state->hMainWnd, TEXT("无法启动 Beyond Compare"), TEXT("错误"), MB_ICONERROR);
        DeleteFile(tempFileOld);
        DeleteFile(tempFileNew);
    }
}

static WCHAR g_ini_path[MAX_PATH_LEN];

static void get_ini_path(void) {
    GetModuleFileNameW(NULL, g_ini_path, MAX_PATH_LEN);
    WCHAR* ext = wcsrchr(g_ini_path, L'.');
    if (ext) *ext = L'\0';
    wcscat(g_ini_path, L".ini");
}

// 读取配置项（ini 内部使用宽字符，与 state 中的 UTF-8 互转，保证中文路径正确）
static void ini_get(const WCHAR* key, const WCHAR* def, char* out, int outSize) {
    WCHAR wbuf[MAX_PATH_LEN];
    GetPrivateProfileStringW(L"Settings", key, def, wbuf, MAX_PATH_LEN, g_ini_path);
    wide_to_utf8(wbuf, out, outSize);
}

static void ini_put(const WCHAR* key, const char* value) {
    WCHAR wbuf[MAX_PATH_LEN];
    utf8_to_wide(value, wbuf, MAX_PATH_LEN);
    WritePrivateProfileStringW(L"Settings", key, wbuf, g_ini_path);
}

void config_load(AppState* state) {
    get_ini_path();
    ini_get(L"GitPath", L"git.exe", state->git_path, MAX_PATH_LEN);
    ini_get(L"BComparePath", L"", state->bcompare_path, MAX_PATH_LEN);
    ini_get(L"LastRepo", L".", state->repo_path, MAX_PATH_LEN);
}

void config_save(AppState* state) {
    get_ini_path();
    ini_put(L"GitPath", state->git_path);
    ini_put(L"BComparePath", state->bcompare_path);
    ini_put(L"LastRepo", state->repo_path);
}

void config_save_repo(AppState* state) {
    get_ini_path();
    ini_put(L"LastRepo", state->repo_path);
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
            ctx->hFont = CreateUIFont();
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
            // hFont 为全局共享字体（CreateUIFont 管理），此处不得删除
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
        wcex.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON));
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassEx(&wcex);
    }
    SettingsCtx* ctx = (SettingsCtx*)malloc(sizeof(SettingsCtx));
    if (!ctx) return;
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

    FileInfo* info = (FileInfo*)get_selected_lparam(state->hFileList);
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

    switch (cmd) {
        case IDM_FILE_VIEW_DIFF:
            on_file_double_click(state);
            break;

        case IDM_FILE_REVERT: {
            if (MessageBox(state->hMainWnd, TEXT("确定要回退该文件的改动吗？"), TEXT("确认回退"), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                append_output_line(state, TEXT("[回退] 正在回退文件..."));
                if (git_checkout_file(state->repo_path, info->path, NULL, 0) == 0) {
                    append_output_line(state, TEXT("[成功] 文件已回退"));
                    refresh_file_list(state);
                } else {
                    append_output_line(state, TEXT("[错误] 回退失败"));
                }
            }
            break;
        }

        case IDM_FILE_STAGE: {
            append_output_line(state, TEXT("[暂存] 正在暂存文件..."));
            if (git_add(state->repo_path, info->path) == 0) {
                append_output_line(state, TEXT("[成功] 文件已暂存"));
                refresh_file_list(state);
            } else {
                append_output_line(state, TEXT("[错误] 暂存失败"));
            }
            break;
        }

        case IDM_FILE_UNSTAGE: {
            append_output_line(state, TEXT("[取消暂存] 正在取消暂存..."));
            if (git_unstage_file(state->repo_path, info->path, NULL, 0) == 0) {
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
                char absPath[MAX_PATH_LEN];
                char fullPathA[MAX_PATH_LEN];
                if (GetFullPathNameA(state->repo_path, MAX_PATH_LEN, absPath, NULL) == 0) {
                    strncpy(absPath, state->repo_path, MAX_PATH_LEN - 1);
                    absPath[MAX_PATH_LEN - 1] = '\0';
                }
                snprintf(fullPathA, MAX_PATH_LEN, "%s/%s", absPath, info->path);
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

    CommitInfo* info = (CommitInfo*)get_selected_lparam(state->hLogList);
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

    switch (cmd) {
        case IDM_LOG_RESET_HARD: {
            TCHAR msg[MAX_PATH_LEN];
            TCHAR whash[32];
            utf8_to_wide(info->hash, whash, 32);
            _sntprintf_s(msg, MAX_PATH_LEN, _TRUNCATE,
                TEXT("确定要回退到版本 %s 吗？\n此操作将丢弃之后的所有提交，且不可撤销！"), whash);
            if (MessageBox(state->hMainWnd, msg, TEXT("确认回退"), MB_YESNO | MB_ICONWARNING) == IDYES) {
                append_output_line(state, TEXT("[回退] 正在回退到指定版本..."));
                if (git_reset_hard(state->repo_path, info->hash, NULL, 0) == 0) {
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
                wide_to_utf8(branchName, branchA, sizeof(branchA));
                append_output_line(state, TEXT("[分支] 正在创建新分支..."));
                if (git_create_branch(state->repo_path, branchA, info->hash, NULL, 0) == 0) {
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
            reflogOutput[0] = '\0';
            append_output_line(state, TEXT("[操作记录] 正在加载 reflog..."));
            if (git_reflog(state->repo_path, reflogOutput, MAX_OUTPUT_SIZE) == 0) {
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
