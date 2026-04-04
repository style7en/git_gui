#ifndef GIT_OPERATIONS_H
#define GIT_OPERATIONS_H

#include <windows.h>

#define MAX_OUTPUT_SIZE    (1024 * 1024)
#define MAX_PATH_LEN       260
#define MAX_CMD_LEN        4096

typedef enum {
    FILE_STATUS_MODIFIED,
    FILE_STATUS_ADDED,
    FILE_STATUS_DELETED,
    FILE_STATUS_UNTRACKED,
    FILE_STATUS_STAGED
} FileStatus;

typedef struct FileInfo {
    char path[MAX_PATH_LEN];
    FileStatus status;
    int staged;
    struct FileInfo* next;
} FileInfo;

typedef struct CommitInfo {
    char hash[48];
    char message[512];
    char author[128];
    char date[64];
    struct CommitInfo* next;
} CommitInfo;

typedef struct CommitFileInfo {
    char path[MAX_PATH_LEN];
    char status[8];
    struct CommitFileInfo* next;
} CommitFileInfo;

extern char g_git_path[MAX_PATH_LEN];

int git_execute(const char* repo_path, const char* args, char* output, int output_size);
int git_get_current_branch(const char* repo_path, char* branch, int size);
FileInfo* git_get_status(const char* repo_path);
void git_free_file_list(FileInfo* list);
CommitInfo* git_get_log(const char* repo_path, int count);
void git_free_commit_list(CommitInfo* list);
CommitFileInfo* git_get_commit_files(const char* repo_path, const char* commit_hash);
void git_free_commit_file_list(CommitFileInfo* list);
int git_get_file_version(const char* repo_path, const char* commit_hash, const char* file_path, char* output, int output_size);
int git_add(const char* repo_path, const char* file_path);
int git_add_all(const char* repo_path);
int git_commit(const char* repo_path, const char* message);
int git_push(const char* repo_path, char* output, int output_size);
int git_pull(const char* repo_path, char* output, int output_size);
int git_is_repository(const char* path);
int git_get_file_head_version(const char* repo_path, const char* file_path, char* output, int output_size);
int git_get_file_staged_version(const char* repo_path, const char* file_path, char* output, int output_size);
void utf8_to_wide(const char* utf8, wchar_t* wide, int size);
void wide_to_utf8(const wchar_t* wide, char* utf8, int size);

#endif
