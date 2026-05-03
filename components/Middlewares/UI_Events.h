#ifndef __UI_EVENTS_H__
#define __UI_EVENTS_H__

// 定义 UI 控制命令
typedef enum {
    UI_CMD_TOGGLE_POWER,  // 开关屏幕
    UI_CMD_SWITCH_PAGE,   // 切换页面
    UI_CMD_MUTE_BUZZER,   // 蜂鸣器静音
} ui_command_t;

void Task_Key_Processor(void *pvParameters);

#endif