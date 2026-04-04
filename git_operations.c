#include "git_operations.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char g_git_path[MAX_PATH_LEN];

// 转义路径中的特殊字符，防止命令注入
static void escape_path_for_git(const char* src, char* dst, int dst_size) {
    int j = 0;
    for (int i = 0; src[i] && j < dst_size - 2; i++) {
        if (src[i] == '\\' || src[i] == '"' || src[i] == '$' || src[i] == '`') {
            dst[j++] = '\\';
        }
        dst[j++] = src[i];
    }
    dst[j] = '\0';
}

void utf8_to_wide(const char* utf8, wchar_t* wide, int size) {
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, size);
}

void wide_to_utf8(const wchar_t* wide, char* utf8, int size) {
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, size, NULL, NULL);
}

int git_execute(const char* repo_path, const char* args, char* output, int output_size) {
    SECURITY_ATTRIBUTES sa;
    HANDLE hReadPipe, hWritePipe;
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

    snprintf(cmdLine, MAX_CMD_LEN, "\"%s\" -c core.quotepath=false -c i18n.logoutputencoding=utf-8 -c i18n.commitencoding=utf-8 %s", g_git_path[0] ? g_git_path : "git.exe", args);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = hWritePipe;
    si.hStdOutput = hWritePipe;
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
                DWORD toRead = (avail < sizeof(buffer) - 1) ? avail : sizeof(buffer) - 1;
                if (ReadFile(hReadPipe, buffer, toRead, &bytesRead, NULL) && bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    if (output && output_size > 0 && totalRead + bytesRead < output_size - 1) {
                        memcpy(output + totalRead, buffer, bytesRead);
                        totalRead += bytesRead;
                        output[totalRead] = '\0';
                    }
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
        DWORD toRead = (avail < sizeof(buffer) - 1) ? avail : sizeof(buffer) - 1;
        if (ReadFile(hReadPipe, buffer, toRead, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            if (output && output_size > 0 && totalRead + bytesRead < output_size - 1) {
                memcpy(output + totalRead, buffer, bytesRead);
                totalRead += bytesRead;
                output[totalRead] = '\0';
            }
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
    return -1;
}

static void parse_status_line(const char* line, FileInfo* info) {
    char x = line[0];
    char y = line[1];
    const char* path = line + 3;

    strncpy(info->path, path, MAX_PATH_LEN - 1);
    info->path[MAX_PATH_LEN - 1] = '\0';
    info->staged = 0;

    char* newline = strchr(info->path, '\n');
    if (newline) *newline = '\0';
    newline = strchr(info->path, '\r');
    if (newline) *newline = '\0';

    if (x == '?' && y == '?') {
        info->status = FILE_STATUS_UNTRACKED;
    } else if (x == 'M' || x == 'A' || x == 'D' || x == 'R' || x == 'C') {
        info->staged = 1;
        switch (x) {
            case 'M': info->status = FILE_STATUS_MODIFIED; break;
            case 'A': info->status = FILE_STATUS_ADDED; break;
            case 'D': info->status = FILE_STATUS_DELETED; break;
            default:  info->status = FILE_STATUS_STAGED; break;
        }
    } else if (y == 'M') {
        info->status = FILE_STATUS_MODIFIED;
    } else if (y == 'D') {
        info->status = FILE_STATUS_DELETED;
    } else if (y == 'A') {
        info->status = FILE_STATUS_ADDED;
        info->staged = 1;
    } else {
        info->status = FILE_STATUS_MODIFIED;
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

        if (strlen(line) >= 3) {
            FileInfo* info = (FileInfo*)malloc(sizeof(FileInfo));
            if (info) {
                memset(info, 0, sizeof(FileInfo));
                parse_status_line(line, info);

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

    snprintf(args, sizeof(args), "log --oneline -%d --format=\"%%h|%%s|%%an|%%cr\"", count);
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

        char* start = line;
        if (*start == '"') start++;

        char* hash = start;
        char* message = strchr(hash, '|');
        char* author = NULL;
        char* date = NULL;

        if (message) {
            *message = '\0';
            message++;
            author = strchr(message, '|');
            if (author) {
                *author = '\0';
                author++;
                date = strchr(author, '|');
                if (date) {
                    *date = '\0';
                    date++;
                    char* quote = strchr(date, '"');
                    if (quote) *quote = '\0';
                }
            }
        }

        if (hash && message && author && date) {
            CommitInfo* info = (CommitInfo*)malloc(sizeof(CommitInfo));
            if (info) {
                memset(info, 0, sizeof(CommitInfo));
                strncpy(info->hash, hash, sizeof(info->hash) - 1);
                info->hash[sizeof(info->hash) - 1] = '\0';
                strncpy(info->message, message, sizeof(info->message) - 1);
                info->message[sizeof(info->message) - 1] = '\0';
                strncpy(info->author, author, sizeof(info->author) - 1);
                info->author[sizeof(info->author) - 1] = '\0';
                strncpy(info->date, date, sizeof(info->date) - 1);
                info->date[sizeof(info->date) - 1] = '\0';

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
    snprintf(args, sizeof(args), "show --name-status --oneline %s", commit_hash);
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
        
        if (sscanf(line, "%7s\t%259[^\n\r]", status, path) == 2 || 
            sscanf(line, "%7s %259[^\n\r]", status, path) == 2) {
            
            CommitFileInfo* info = (CommitFileInfo*)malloc(sizeof(CommitFileInfo));
            if (info) {
                memset(info, 0, sizeof(CommitFileInfo));
                snprintf(info->status, sizeof(info->status), "%s", status);
                snprintf(info->path, sizeof(info->path), "%s", path);
                
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
