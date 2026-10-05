#include "git_operations.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char g_git_path[MAX_PATH_LEN];

void git_set_git_path(const char* path) {
    if (!path) {
        g_git_path[0] = '\0';
        return;
    }
    strncpy(g_git_path, path, MAX_PATH_LEN - 1);
    g_git_path[MAX_PATH_LEN - 1] = '\0';
}

// 转义 git 双引号参数中的特殊字符。
// 注意：命令由 CreateProcess 直接执行，不经过 shell，因此只需处理
// git 自身在双引号内的转义规则：反斜杠和双引号。
static void escape_path_for_git(const char* src, char* dst, int dst_size) {
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 2; i++) {
        if (src[i] == '\\' || src[i] == '"') {
            dst[j++] = '\\';
        }
        dst[j++] = src[i];
    }
    dst[j] = '\0';
}

void utf8_to_wide(const char* utf8, wchar_t* wide, int size) {
    if (!utf8 || !wide || size <= 0) {
        if (wide && size > 0) wide[0] = L'\0';
        return;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, size) == 0) {
        wide[0] = L'\0';
    }
}

void wide_to_utf8(const wchar_t* wide, char* utf8, int size) {
    if (!wide || !utf8 || size <= 0) {
        if (utf8 && size > 0) utf8[0] = '\0';
        return;
    }
    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, size, NULL, NULL) == 0) {
        utf8[0] = '\0';
    }
}

// 将管道数据追加到输出缓冲，超出容量时按剩余空间部分拷贝（而非整块丢弃）
static void append_pipe_output(char* output, int output_size, DWORD* totalRead,
                               const char* buffer, DWORD bytesRead) {
    if (!output || output_size <= 0) return;

    DWORD space = (DWORD)output_size - 1 - *totalRead;
    DWORD toCopy = (bytesRead < space) ? bytesRead : space;
    if (toCopy > 0) {
        memcpy(output + *totalRead, buffer, toCopy);
        *totalRead += toCopy;
        output[*totalRead] = '\0';
    }
}

int git_execute(const char* repo_path, const char* args, char* output, int output_size) {
    SECURITY_ATTRIBUTES sa;
    HANDLE hReadPipe, hWritePipe, hNulInput;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD bytesRead, totalRead = 0;
    BOOL success;
    char cmdLine[MAX_CMD_LEN];

    if (output && output_size > 0) {
        output[0] = '\0';
    }

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return -1;
    }

    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    // 为子进程提供有效的 stdin（NUL），避免 git 在需要读取标准输入时挂起
    hNulInput = CreateFileA("NUL", GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                            OPEN_EXISTING, 0, NULL);

    snprintf(cmdLine, MAX_CMD_LEN, "\"%s\" -c core.quotepath=false -c i18n.logoutputencoding=utf-8 -c i18n.commitencoding=utf-8 %s", g_git_path[0] ? g_git_path : "git.exe", args);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = hWritePipe;
    si.hStdOutput = hWritePipe;
    si.hStdInput = (hNulInput != INVALID_HANDLE_VALUE) ? hNulInput : NULL;
    si.dwFlags |= STARTF_USESTDHANDLES;

    si.wShowWindow = SW_HIDE;

    ZeroMemory(&pi, sizeof(pi));

    success = CreateProcessA(
        NULL,
        cmdLine,
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        repo_path,
        &si,
        &pi
    );

    if (hNulInput != INVALID_HANDLE_VALUE) {
        CloseHandle(hNulInput);
    }

    if (!success) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return -1;
    }

    CloseHandle(hWritePipe);

    char buffer[4096];
    BOOL processDone = FALSE;
    DWORD avail;

    while (!processDone) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (PeekNamedPipe(hReadPipe, NULL, 0, NULL, &avail, NULL)) {
            if (avail > 0) {
                DWORD toRead = (avail < sizeof(buffer) - 1) ? avail : (DWORD)(sizeof(buffer) - 1);
                if (ReadFile(hReadPipe, buffer, toRead, &bytesRead, NULL) && bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    append_pipe_output(output, output_size, &totalRead, buffer, bytesRead);
                }
            } else {
                DWORD waitResult = WaitForSingleObject(pi.hProcess, 50);
                if (waitResult == WAIT_OBJECT_0) {
                    processDone = TRUE;
                }
            }
        } else {
            break;
        }
    }

    while (PeekNamedPipe(hReadPipe, NULL, 0, NULL, &avail, NULL) && avail > 0) {
        DWORD toRead = (avail < sizeof(buffer) - 1) ? avail : (DWORD)(sizeof(buffer) - 1);
        if (ReadFile(hReadPipe, buffer, toRead, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            append_pipe_output(output, output_size, &totalRead, buffer, bytesRead);
        }
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hReadPipe);

    return (int)exitCode;
}

int git_get_current_branch(const char* repo_path, char* branch, int size) {
    char output[1024];
    int result;

    result = git_execute(repo_path, "rev-parse --abbrev-ref HEAD", output, sizeof(output));

    if (result == 0 && output[0] != '\0') {
        char* newline = strchr(output, '\n');
        if (newline) *newline = '\0';
        newline = strchr(output, '\r');
        if (newline) *newline = '\0';

        strncpy(branch, output, size - 1);
        branch[size - 1] = '\0';
        return 0;
    }

    strncpy(branch, "unknown", size - 1);
    branch[size - 1] = '\0';
    return -1;
}

// 去除字符串末尾的 \r / \n
static void strip_eol(char* s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
}

// 去除 git 对含特殊字符路径添加的包裹双引号
static void strip_quotes(char* s) {
    size_t len = strlen(s);
    if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
        s[len - 1] = '\0';
        memmove(s, s + 1, len - 1);
    }
}

static void parse_status_line(const char* line, FileInfo* info) {
    char x = line[0];
    char y = line[1];
    const char* path = line + 3;

    strncpy(info->path, path, MAX_PATH_LEN - 1);
    info->path[MAX_PATH_LEN - 1] = '\0';
    info->staged = 0;
    strip_eol(info->path);

    // 重命名/复制在 porcelain 中形如 "ORIG -> PATH"，取新路径
    char* arrow = strstr(info->path, " -> ");
    if (arrow) {
        memmove(info->path, arrow + 4, strlen(arrow + 4) + 1);
    }
    strip_quotes(info->path);

    if (x == '?' && y == '?') {
        info->status = FILE_STATUS_UNTRACKED;
        return;
    }

    // 先看暂存区状态（X 列），其优先级高于工作区
    switch (x) {
        case 'M': info->status = FILE_STATUS_MODIFIED; info->staged = 1; return;
        case 'A': info->status = FILE_STATUS_ADDED;    info->staged = 1; return;
        case 'D': info->status = FILE_STATUS_DELETED;  info->staged = 1; return;
        case 'R':
        case 'C': info->status = FILE_STATUS_STAGED;   info->staged = 1; return;
        default: break;
    }

    // 再看工作区状态（Y 列）
    switch (y) {
        case 'M': info->status = FILE_STATUS_MODIFIED; break;
        case 'D': info->status = FILE_STATUS_DELETED;  break;
        case 'A': info->status = FILE_STATUS_ADDED;    break;
        default:  info->status = FILE_STATUS_MODIFIED; break;
    }
}

FileInfo* git_get_status(const char* repo_path) {
    char* output = (char*)malloc(MAX_OUTPUT_SIZE);
    if (!output) return NULL;

    FileInfo* head = NULL;
    FileInfo* tail = NULL;
    char* line;
    int result;

    result = git_execute(repo_path, "status --porcelain", output, MAX_OUTPUT_SIZE);

    if (result != 0 || output[0] == '\0') {
        free(output);
        return NULL;
    }

    line = output;
    while (line && *line) {
        char* nextLine = strchr(line, '\n');
        if (nextLine) {
            *nextLine = '\0';
            nextLine++;
        }

        if (strlen(line) >= 4) {
            FileInfo* info = (FileInfo*)malloc(sizeof(FileInfo));
            if (info) {
                memset(info, 0, sizeof(FileInfo));
                parse_status_line(line, info);
                info->next = NULL;

                if (tail) {
                    tail->next = info;
                    tail = info;
                } else {
                    head = info;
                    tail = info;
                }
            }
        }

        line = nextLine;
    }

    free(output);
    return head;
}

void git_free_file_list(FileInfo* list) {
    while (list) {
        FileInfo* next = list->next;
        free(list);
        list = next;
    }
}

CommitInfo* git_get_log(const char* repo_path, int count) {
    char* output = (char*)malloc(MAX_OUTPUT_SIZE);
    if (!output) return NULL;

    char args[256];
    CommitInfo* head = NULL;
    CommitInfo* tail = NULL;
    char* line;
    int result;

    // 用 0x1f 分隔字段，避免提交信息/作者名中的普通字符导致解析错位
    snprintf(args, sizeof(args),
             "log -%d --format=%%h%c%%s%c%%an%c%%cr",
             count, GIT_FIELD_SEP, GIT_FIELD_SEP, GIT_FIELD_SEP);
    result = git_execute(repo_path, args, output, MAX_OUTPUT_SIZE);

    if (result != 0 || output[0] == '\0') {
        free(output);
        return NULL;
    }

    line = output;
    while (line && *line) {
        char* nextLine = strchr(line, '\n');
        if (nextLine) {
            *nextLine = '\0';
            nextLine++;
        }

        char* f1 = strchr(line, GIT_FIELD_SEP);
        char* f2 = f1 ? strchr(f1 + 1, GIT_FIELD_SEP) : NULL;
        char* f3 = f2 ? strchr(f2 + 1, GIT_FIELD_SEP) : NULL;

        if (f1 && f2 && f3) {
            *f1 = '\0';
            *f2 = '\0';
            *f3 = '\0';

            CommitInfo* info = (CommitInfo*)malloc(sizeof(CommitInfo));
            if (info) {
                memset(info, 0, sizeof(CommitInfo));
                strncpy(info->hash, line, sizeof(info->hash) - 1);
                strncpy(info->message, f1 + 1, sizeof(info->message) - 1);
                strncpy(info->author, f2 + 1, sizeof(info->author) - 1);
                strncpy(info->date, f3 + 1, sizeof(info->date) - 1);
                strip_eol(info->date);
                info->next = NULL;

                if (tail) {
                    tail->next = info;
                    tail = info;
                } else {
                    head = info;
                    tail = info;
                }
            }
        }

        line = nextLine;
    }

    free(output);
    return head;
}

void git_free_commit_list(CommitInfo* list) {
    while (list) {
        CommitInfo* next = list->next;
        free(list);
        list = next;
    }
}

int git_add(const char* repo_path, const char* file_path) {
    char args[MAX_CMD_LEN];
    char escaped[MAX_PATH_LEN * 2];
    escape_path_for_git(file_path, escaped, sizeof(escaped));
    snprintf(args, sizeof(args), "add \"%s\"", escaped);
    return git_execute(repo_path, args, NULL, 0);
}

int git_add_all(const char* repo_path) {
    return git_execute(repo_path, "add -A", NULL, 0);
}

int git_checkout_file(const char* repo_path, const char* file_path, char* output, int output_size) {
    char args[MAX_CMD_LEN];
    char escaped[MAX_PATH_LEN * 2];
    escape_path_for_git(file_path, escaped, sizeof(escaped));
    snprintf(args, sizeof(args), "checkout -- \"%s\"", escaped);
    return git_execute(repo_path, args, output, output_size);
}

int git_unstage_file(const char* repo_path, const char* file_path, char* output, int output_size) {
    char args[MAX_CMD_LEN];
    char escaped[MAX_PATH_LEN * 2];
    escape_path_for_git(file_path, escaped, sizeof(escaped));
    snprintf(args, sizeof(args), "reset HEAD -- \"%s\"", escaped);
    return git_execute(repo_path, args, output, output_size);
}

int git_reset_hard(const char* repo_path, const char* commit, char* output, int output_size) {
    char args[MAX_CMD_LEN];
    char escaped[MAX_PATH_LEN * 2];
    escape_path_for_git(commit, escaped, sizeof(escaped));
    snprintf(args, sizeof(args), "reset --hard %s", escaped);
    return git_execute(repo_path, args, output, output_size);
}

int git_create_branch(const char* repo_path, const char* branch, const char* commit, char* output, int output_size) {
    char args[MAX_CMD_LEN];
    char escapedBranch[512];
    char escapedCommit[MAX_PATH_LEN * 2];
    escape_path_for_git(branch, escapedBranch, sizeof(escapedBranch));
    escape_path_for_git(commit, escapedCommit, sizeof(escapedCommit));
    snprintf(args, sizeof(args), "checkout -b \"%s\" %s", escapedBranch, escapedCommit);
    return git_execute(repo_path, args, output, output_size);
}

int git_reflog(const char* repo_path, char* output, int output_size) {
    return git_execute(repo_path, "reflog --pretty=format:\"%h  %gs\"", output, output_size);
}

int git_commit(const char* repo_path, const char* message) {
    char tempPath[MAX_PATH_LEN];
    char tempFile[MAX_PATH_LEN];

    GetTempPathA(MAX_PATH_LEN, tempPath);
    GetTempFileNameA(tempPath, "git_msg", 0, tempFile);

    HANDLE hFile = CreateFileA(tempFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return -1;
    }

    DWORD written;
    WriteFile(hFile, message, (DWORD)strlen(message), &written, NULL);
    CloseHandle(hFile);

    char args[MAX_CMD_LEN];
    snprintf(args, sizeof(args), "commit -F \"%s\"", tempFile);
    int result = git_execute(repo_path, args, NULL, 0);

    DeleteFileA(tempFile);
    return result;
}

int git_push(const char* repo_path, char* output, int output_size) {
    return git_execute(repo_path, "push", output, output_size);
}

int git_pull(const char* repo_path, char* output, int output_size) {
    return git_execute(repo_path, "pull", output, output_size);
}

int git_is_repository(const char* path) {
    char output[256];
    int result = git_execute(path, "rev-parse --git-dir", output, sizeof(output));
    return (result == 0);
}

int git_get_file_head_version(const char* repo_path, const char* file_path, char* output, int output_size) {
    char args[MAX_CMD_LEN];
    char escaped[MAX_PATH_LEN * 2];
    escape_path_for_git(file_path, escaped, sizeof(escaped));
    snprintf(args, sizeof(args), "show HEAD:\"%s\"", escaped);
    return git_execute(repo_path, args, output, output_size);
}

int git_get_file_staged_version(const char* repo_path, const char* file_path, char* output, int output_size) {
    char args[MAX_CMD_LEN];
    char escaped[MAX_PATH_LEN * 2];
    escape_path_for_git(file_path, escaped, sizeof(escaped));
    snprintf(args, sizeof(args), "show :\"%s\"", escaped);
    return git_execute(repo_path, args, output, output_size);
}

CommitFileInfo* git_get_commit_files(const char* repo_path, const char* commit_hash) {
    char* output = (char*)malloc(MAX_OUTPUT_SIZE);
    if (!output) return NULL;

    char args[256];
    snprintf(args, sizeof(args), "show --name-status --format=%%H %s", commit_hash);
    int result = git_execute(repo_path, args, output, MAX_OUTPUT_SIZE);

    CommitFileInfo* head = NULL;
    CommitFileInfo* tail = NULL;

    if (result != 0 || output[0] == '\0') {
        free(output);
        return NULL;
    }

    char* line = strchr(output, '\n');
    if (line) line++;

    while (line && *line) {
        char* nextLine = strchr(line, '\n');
        if (nextLine) {
            *nextLine = '\0';
            nextLine++;
        }

        while (*line == ' ' || *line == '\t') line++;
        if (*line == '\0') {
            line = nextLine;
            continue;
        }

        char status[8] = {0};
        char path[MAX_PATH_LEN] = {0};

        if (sscanf(line, "%7s %259[^\n\r]", status, path) == 2) {
            // 重命名/复制为 "R100\told\tnew"（或空格分隔），取新路径
            char* tab = strchr(path, '\t');
            if (tab) {
                memmove(path, tab + 1, strlen(tab + 1) + 1);
            }
            strip_quotes(path);

            CommitFileInfo* info = (CommitFileInfo*)malloc(sizeof(CommitFileInfo));
            if (info) {
                memset(info, 0, sizeof(CommitFileInfo));
                snprintf(info->status, sizeof(info->status), "%s", status);
                snprintf(info->path, sizeof(info->path), "%s", path);
                info->next = NULL;

                if (tail) {
                    tail->next = info;
                    tail = info;
                } else {
                    head = info;
                    tail = info;
                }
            }
        }

        line = nextLine;
    }

    free(output);
    return head;
}

void git_free_commit_file_list(CommitFileInfo* list) {
    while (list) {
        CommitFileInfo* next = list->next;
        free(list);
        list = next;
    }
}

int git_get_file_version(const char* repo_path, const char* commit_hash, const char* file_path, char* output, int output_size) {
    char args[MAX_CMD_LEN];
    char escaped[MAX_PATH_LEN * 2];
    escape_path_for_git(file_path, escaped, sizeof(escaped));
    snprintf(args, sizeof(args), "show %s:\"%s\"", commit_hash, escaped);
    return git_execute(repo_path, args, output, output_size);
}
