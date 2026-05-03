#include "Key.h"
#include "UI_Events.h"
#include "Buzzer.h"

static bool is_muted = false;

void Task_Key_Processor(void *pvParameters)
{
    // 从参数中获取 UI 消息队列句柄
    QueueHandle_t ui_queue = (QueueHandle_t)pvParameters;
    key_result_t keyEvt;
    ui_command_t cmd;

    while (1) {
        // 1. 等待底层按键事件
        if (Key_Get_Event(&keyEvt, portMAX_DELAY)) {
            
            // 2. 根据按键映射业务逻辑
            if (keyEvt.id == KEY_1) {
                if (keyEvt.event == KEY_EVENT_SINGLE_CLICK) {
                    // 1. 立刻命令蜂鸣器闭嘴（直接操作硬件或发送最高优先级任务通知）
					Buzzer_Set_Mute(true);
					
					// 2. 逻辑自锁：标记为静音状态
					is_muted = true;
                } 
                else if (keyEvt.event == KEY_EVENT_LONG_PRESS) {
                    // 逻辑 C: 发送开关屏命令
                    cmd = UI_CMD_TOGGLE_POWER;
                    xQueueSend(ui_queue, &cmd, 0);
                }
            }
            else if (keyEvt.id == KEY_2 && keyEvt.event == KEY_EVENT_SINGLE_CLICK) {
                // 逻辑 D: 发送切屏命令
                cmd = UI_CMD_SWITCH_PAGE;
                xQueueSend(ui_queue, &cmd, 0);
            }
        }
    }
}