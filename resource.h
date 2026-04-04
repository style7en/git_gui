#ifndef RESOURCE_H
#define RESOURCE_H

// 控件 ID 定义
#define ID_MAIN_WINDOW      1

// 工具栏按钮 ID
#define ID_BTN_PUSH         1002
#define ID_BTN_PULL         1003
#define ID_BTN_REFRESH      1004
#define ID_BTN_SELECT_REPO  1005
#define ID_BTN_SETTINGS     1006

// Tab 控件
#define ID_TAB_CONTROL      2001

// 状态页控件
#define ID_FILE_LIST        3001
#define ID_COMMIT_MSG       3002
#define ID_BTN_STAGE        3003
#define ID_BTN_COMMIT_NOW   3004
#define ID_BTN_STAGE_ALL    3005

// 日志页控件
#define ID_LOG_LIST         4001
#define ID_OUTPUT           4002
#define ID_COMMIT_FILE_LIST 4003

// 菜单 ID
#define IDM_FILE_OPENREPO   5001
#define IDM_FILE_EXIT       5002
#define IDM_HELP_ABOUT      5003
#define IDM_FILE_SETTINGS   5004

// 设置对话框控件
#define ID_SETTINGS_GIT     7001
#define ID_SETTINGS_BROWSE_GIT 7002
#define ID_SETTINGS_BCOMPARE 7003
#define ID_SETTINGS_BROWSE_BCOMPARE 7004
#define ID_SETTINGS_SAVE    7005
#define ID_SETTINGS_CANCEL  7006

// 对话框 ID
#define IDD_SETTINGS        8001

// 状态栏部分
#define ID_STATUS_BAR       6001

// 文件列表右键菜单
#define IDM_FILE_REVERT     9001
#define IDM_FILE_VIEW_DIFF  9002
#define IDM_FILE_DELETE     9003
#define IDM_FILE_STAGE      9004
#define IDM_FILE_UNSTAGE    9005

// 日志列表右键菜单
#define IDM_LOG_RESET_HARD  9010
#define IDM_LOG_REVERT      9011
#define IDM_LOG_COPY_HASH   9012
#define IDM_LOG_NEW_BRANCH  9013
#define IDM_LOG_CHERRYPICK  9014
#define IDM_LOG_VIEW_REFLOG 9015

#endif // RESOURCE_H
