#include "GetBaLevel.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

static const char *TAG = "BatteryLevel";

// --- 全局变量定义 ---
bool Battery_Level_Initialized = false;
uint8_t Battery_Level_Percent = 0;
float Battery_Voltage = 0.0f;

// ADC校准特性
static esp_adc_cal_characteristics_t *adc_chars = NULL;

// --- 内部函数声明 ---
static esp_err_t Battery_Adc_Calibration_Init(void);

/**
 * @brief 初始化电池电量检测模块
 */
esp_err_t Battery_Level_Init(void)
{
    esp_err_t ret = ESP_OK;
    
    // 配置ADC通道
    ret = adc1_config_width(BATTERY_ADC_WIDTH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC宽度配置失败: %d", ret);
        return ret;
    }
    
    ret = adc1_config_channel_atten(BATTERY_ADC_CHANNEL, BATTERY_ADC_ATTEN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC通道配置失败: %d", ret);
        return ret;
    }
    
    // 初始化ADC校准
    ret = Battery_Adc_Calibration_Init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC校准初始化失败: %d", ret);
        return ret;
    }
    
    Battery_Level_Initialized = true;
    ESP_LOGI(TAG, "电池电量检测模块初始化完成");
    
    // 立即读取一次电池电量
    Battery_Read_Voltage(&Battery_Voltage);
    Battery_Level_Percent = Battery_Calculate_Percentage(Battery_Voltage);
    
    ESP_LOGI(TAG, "初始电池状态: %.2fV, %d%%", Battery_Voltage, Battery_Level_Percent);
    
    return ESP_OK;
}

/**
 * @brief 初始化ADC校准特性
 */
static esp_err_t Battery_Adc_Calibration_Init(void)
{
    // 为ADC校准特性分配内存
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    if (adc_chars == NULL) {
        ESP_LOGE(TAG, "ADC校准特性内存分配失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化ADC校准特性
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(BATTERY_ADC_UNIT, 
                                                             BATTERY_ADC_ATTEN,
                                                             BATTERY_ADC_WIDTH,
                                                             REFERENCE_VOLTAGE,
                                                             adc_chars);
    
    switch (val_type) {
        case ESP_ADC_CAL_VAL_EFUSE_VREF:
            ESP_LOGI(TAG, "使用eFuse Vref进行ADC校准");
            break;
        case ESP_ADC_CAL_VAL_EFUSE_TP:
            ESP_LOGI(TAG, "使用两点校准进行ADC校准");
            break;
        default:
            ESP_LOGW(TAG, "使用默认Vref进行ADC校准");
            break;
    }
    
    return ESP_OK;
}

/**
 * @brief 读取电池电压（原始ADC值）
 */
uint32_t Battery_Read_Adc_Value()
{
    if (!Battery_Level_Initialized) {
        ESP_LOGE(TAG, "电池电量检测模块未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    // 读取ADC原始值
    int raw_adc = adc1_get_raw(BATTERY_ADC_CHANNEL);
    if (raw_adc < 0) {
        ESP_LOGE(TAG, "ADC读取失败: %d", raw_adc);
        return ESP_FAIL;
    }
    return (uint32_t)raw_adc;
}

/**
 * @brief 读取电池电压（转换为电压值）
 */
esp_err_t Battery_Read_Voltage(float *voltage)
{
    if (!Battery_Level_Initialized) {
        ESP_LOGE(TAG, "电池电量检测模块未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (voltage == NULL) {
        ESP_LOGE(TAG, "电压指针为空");
        return ESP_ERR_INVALID_ARG;
    }

    // 获取adc值和简单滤波，采样次数减少为16次，防止阻塞
    uint32_t adc_value = 0;
    const uint8_t sample_count = 16;
    for (uint8_t i = 0; i < sample_count; ++i) {
        adc_value += Battery_Read_Adc_Value();
        vTaskDelay(1 / portTICK_PERIOD_MS); // 每次采样间隔1ms，避免阻塞
    }
    adc_value /= sample_count;

    // 使用校准特性将ADC值转换为电压
    uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(adc_value, adc_chars);

    // 转换为伏特并考虑电压分压比
    *voltage = (float)voltage_mv / 1000.0f * VOLTAGE_DIVIDER_RATIO;

    return ESP_OK;
}

/**
 * @brief 计算电池电量百分比-用3.3v测试的
 */
uint8_t Battery_Calculate_Percentage_Test33V(float voltage)
{
    // 限制电压在有效范围内
    if (voltage >= BATTERY_FULL_VOLTAGE) {
        return 100;
    }
    if (voltage <= BATTERY_EMPTY_VOLTAGE) {
        return 0;
    }
    
    // 线性插值计算电量百分比
    float percentage = ((voltage - BATTERY_EMPTY_VOLTAGE) / 
                       (BATTERY_FULL_VOLTAGE - BATTERY_EMPTY_VOLTAGE)) * 100.0f;
    
    // 限制在0-100范围内
    if (percentage < 0) {
        percentage = 0;
    } else if (percentage > 100) {
        percentage = 100;
    }
    
    return (uint8_t)percentage;
}

/**
 * @brief 计算电池电量百分比-真正用到电池上的
 */

uint8_t Battery_Calculate_Percentage(float voltage)
{
    uint8_t percentage = voltage * 100 / BATTERY_FULL_VOLTAGE;
    return percentage >= 100?100:percentage;
}

/**
 * @brief 获取当前电池电量百分比
 */
uint8_t Battery_Get_Level(void)
{
    return Battery_Level_Percent;
}

/**
 * @brief 获取当前电池电压
 */
float Battery_Get_Voltage(void)
{
    return Battery_Voltage;
}