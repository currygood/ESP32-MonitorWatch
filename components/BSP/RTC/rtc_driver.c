#include "rtc_driver.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include <time.h>
#include <string.h>
#include <sys/time.h>

// 全局变量
static bool rtc_initialized = false;
static int8_t timezone_offset = RTC_TIMEZONE_OFFSET;
static uint64_t system_start_time = 0;

// 月份天数表（非闰年）
static const uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// 检查是否为闰年
static bool is_leap_year(uint16_t year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// 验证时间有效性
static bool validate_time(const rtc_time_t *time) {
    if (time->seconds > 59 || time->minutes > 59 || time->hours > 23 ||
        time->month < 1 || time->month > 12 || time->year < 2000 || time->year > 2099) {
        return false;
    }
    
    uint8_t max_days = days_in_month[time->month - 1];
    if (time->month == 2 && is_leap_year(time->year)) {
        max_days = 29;
    }
    
    return time->day >= 1 && time->day <= max_days;
}

// 从struct tm转换到rtc_time_t
static void tm_to_rtc_time(const struct tm *tm, rtc_time_t *rtc_time) {
    rtc_time->seconds = tm->tm_sec;
    rtc_time->minutes = tm->tm_min;
    rtc_time->hours = tm->tm_hour;
    rtc_time->day = tm->tm_mday;
    rtc_time->month = tm->tm_mon + 1;
    rtc_time->year = tm->tm_year + 1900;
    rtc_time->weekday = tm->tm_wday;
}

// 从rtc_time_t转换到struct tm
static void rtc_time_to_tm(const rtc_time_t *rtc_time, struct tm *tm) {
    tm->tm_sec = rtc_time->seconds;
    tm->tm_min = rtc_time->minutes;
    tm->tm_hour = rtc_time->hours;
    tm->tm_mday = rtc_time->day;
    tm->tm_mon = rtc_time->month - 1;
    tm->tm_year = rtc_time->year - 1900;
    tm->tm_wday = rtc_time->weekday;
    tm->tm_isdst = -1; // 自动判断夏令时
}

// 初始化RTC模块（使用ESP32-S3内部RTC）
esp_err_t Rtc_Init(void) {
    if (rtc_initialized) {
        return ESP_OK;
    }
    
    // 设置时区
    setenv("TZ", "CST-8", 1); // 中国标准时间 UTC+8
    tzset();
    
    // 记录系统启动时间
    system_start_time = esp_timer_get_time() / 1000; // 转换为毫秒
    
    rtc_initialized = true;
    
    return ESP_OK;
}

// 设置RTC时间
esp_err_t Rtc_Set_Time(const rtc_time_t *time) {
    if (!rtc_initialized) {
        return ESP_FAIL;
    }
    
    if (!validate_time(time)) {
        return ESP_FAIL;
    }
    
    struct tm tm_time;
    rtc_time_to_tm(time, &tm_time);
    
    time_t unix_time = mktime(&tm_time);
    if (unix_time == (time_t)-1) {
        return ESP_FAIL;
    }
    
    struct timeval tv = {
        .tv_sec = unix_time,
        .tv_usec = 0
    };
    
    return settimeofday(&tv, NULL);
}

// 获取RTC时间
esp_err_t Rtc_Get_Time(rtc_time_t *time) {
    if (!rtc_initialized) {
        return ESP_FAIL;
    }
    
    struct tm tm_time;
    struct timeval tv;
    
    // 使用gettimeofday替代time()函数
    if (gettimeofday(&tv, NULL) != 0) {
        return ESP_FAIL;
    }
    
    time_t now = tv.tv_sec;
    localtime_r(&now, &tm_time);
    
    tm_to_rtc_time(&tm_time, time);
    
    return ESP_OK;
}

// 检查RTC是否已初始化
bool Rtc_Is_Initialized(void) {
    return rtc_initialized;
}

// 从Unix时间戳设置RTC时间
esp_err_t Rtc_Set_From_Unix(uint32_t unix_timestamp) {
    if (!rtc_initialized) {
        return ESP_FAIL;
    }
    
    struct timeval tv = {
        .tv_sec = unix_timestamp,
        .tv_usec = 0
    };
    
    return settimeofday(&tv, NULL);
}

// 获取Unix时间戳
esp_err_t Rtc_Get_Unix(uint32_t *unix_timestamp) {
    if (!rtc_initialized) {
        return ESP_FAIL;
    }
    
    struct timeval tv;
    
    // 使用gettimeofday替代time()函数
    if (gettimeofday(&tv, NULL) != 0) {
        return ESP_FAIL;
    }
    
    *unix_timestamp = (uint32_t)tv.tv_sec;
    
    return ESP_OK;
}

// 格式化时间字符串
esp_err_t Rtc_Format_Time_String(const rtc_time_t *time, char *buffer, size_t buffer_size) {
    if (buffer_size < 20) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    struct tm tm_time;
    rtc_time_to_tm(time, &tm_time);
    
    strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", &tm_time);
    
    return ESP_OK;
}

// 设置时区
esp_err_t Rtc_Set_Timezone(int8_t timezone_offset) {
    if (!rtc_initialized) {
        return ESP_FAIL;
    }
    
    char tz_string[16];
    if (timezone_offset >= 0) {
        snprintf(tz_string, sizeof(tz_string), "UTC+%d", timezone_offset);
    } else {
        snprintf(tz_string, sizeof(tz_string), "UTC%d", timezone_offset);
    }
    
    setenv("TZ", tz_string, 1);
    tzset();
    
    return ESP_OK;
}

// 获取系统启动时间（毫秒）
uint64_t Rtc_Get_Uptime_Ms(void) {
    if (!rtc_initialized) {
        return 0;
    }
    
    uint64_t current_time = esp_timer_get_time() / 1000; // 转换为毫秒
    return current_time - system_start_time;
}

// 深度睡眠时间设置
esp_err_t Rtc_Set_DeepSleep_Timeout(uint64_t sleep_time_us) {
    return esp_sleep_enable_timer_wakeup(sleep_time_us);
}

// 获取RTC内存数据
esp_err_t Rtc_Read_Memory(uint32_t offset, void *data, size_t size) {
    if (offset + size > 0x2000) { // RTC内存大小限制
        return ESP_ERR_INVALID_SIZE;
    }
    
    // 从RTC内存读取数据
    uint8_t *rtc_mem = (uint8_t *)(0x3FF80000 + offset);
    memcpy(data, rtc_mem, size);
    
    return ESP_OK;
}

// 写入RTC内存数据
esp_err_t Rtc_Write_Memory(uint32_t offset, const void *data, size_t size) {
    if (offset + size > 0x2000) { // RTC内存大小限制
        return ESP_ERR_INVALID_SIZE;
    }
    
    // 写入RTC内存数据
    uint8_t *rtc_mem = (uint8_t *)(0x3FF80000 + offset);
    memcpy(rtc_mem, data, size);
    
    return ESP_OK;
}