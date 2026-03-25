#ifndef RTC_DRIVER_H
#define RTC_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_attr.h"

// --- RTC时间结构体 ---
typedef struct {
    uint8_t seconds;     // 秒 (0-59)
    uint8_t minutes;     // 分 (0-59)
    uint8_t hours;       // 时 (0-23)
    uint8_t day;         // 日 (1-31)
    uint8_t month;       // 月 (1-12)
    uint16_t year;       // 年 (2000-2099)
    uint8_t weekday;     // 星期 (0-6, 0=周日)
} rtc_time_t;

// --- RTC配置宏 ---
#define RTC_TIMEZONE_OFFSET 8    // 时区偏移（北京时间 UTC+8）
#define RTC_DST_OFFSET 0         // 夏令时偏移

// --- RTC错误码 ---
#define RTC_ERR_INVALID_TIME 0x01
#define RTC_ERR_NOT_INIT     0x02

// --- RTC函数声明 ---

// 初始化RTC模块（使用ESP32-S3内部RTC）
esp_err_t Rtc_Init(void);

// 设置RTC时间
esp_err_t Rtc_Set_Time(const rtc_time_t *time);

// 获取RTC时间
esp_err_t Rtc_Get_Time(rtc_time_t *time);

// 检查RTC是否已初始化
bool Rtc_Is_Initialized(void);

// 从Unix时间戳设置RTC时间
esp_err_t Rtc_Set_From_Unix(uint32_t unix_timestamp);

// 获取Unix时间戳
esp_err_t Rtc_Get_Unix(uint32_t *unix_timestamp);

// 格式化时间字符串
esp_err_t Rtc_Format_Time_String(const rtc_time_t *time, char *buffer, size_t buffer_size);

// 设置时区
esp_err_t Rtc_Set_Timezone(int8_t timezone_offset);

// 获取系统启动时间（毫秒）
uint64_t Rtc_Get_Uptime_Ms(void);

// 深度睡眠时间设置
esp_err_t Rtc_Set_DeepSleep_Timeout(uint64_t sleep_time_us);

// 获取RTC内存数据
esp_err_t Rtc_Read_Memory(uint32_t offset, void *data, size_t size);

// 写入RTC内存数据
esp_err_t Rtc_Write_Memory(uint32_t offset, const void *data, size_t size);

#endif // RTC_DRIVER_H