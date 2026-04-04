#ifndef UI_CALLBACKS_H
#define UI_CALLBACKS_H

#include <windows.h>
#include "git_operations.h"

typedef struct AppState {
    char repo_path[MAX_PATH_LEN];
    char current_branch[128];
    char selected_commit_hash[48];
    char git_path[MAX_PATH_LEN];
    char bcompare_path[MAX_PATH_LEN];
    FileInfo* file_list;
    CommitInfo* commit_list;
    CommitFileInfo* commit_file_list;
    HWND hMainWnd;
    HWND hFileList;
    HWND hLogList;
    HWND hCommitFileList;
    HWND hCommitMsg;
    HWND hLabelCommit;
    HWND hBtnStage;
    HWND hBtnStageAll;
    HWND hBtnCommitNow;
    HWND hLabelOutput;
    HWND hOutput;
    HWND hStatusBar;
    HWND hTabControl;
    int currentTab;
} AppState;

void app_state_init(AppState* state);
void app_state_cleanup(AppState* state);
void append_output(AppState* state, const TCHAR* text);
void append_output_line(AppState* state, const TCHAR* text);
void clear_output(AppState* state);
void refresh_file_list(AppState* state);
void refresh_log_list(AppState* state);
void refresh_branch_info(AppState* state);
void on_commit_clicked(AppState* state);
void on_push_clicked(AppState* state);
void on_pull_clicked(AppState* state);
void on_refresh_clicked(AppState* state);
void on_stage_clicked(AppState* state);
void on_stage_all_clicked(AppState* state);
void on_select_repo_clicked(AppState* state);
void on_tab_changed(AppState* state, int tabIndex);
void update_status_bar(AppState* state);
void on_file_double_click(AppState* state);
void on_log_double_click(AppState* state);
void on_commit_file_double_click(AppState* state);
void on_settings_clicked(AppState* state);
void on_file_list_context_menu(AppState* state, POINT pt);
void on_log_list_context_menu(AppState* state, POINT pt);
void config_load(AppState* state);
void config_save(AppState* state);

#endif
