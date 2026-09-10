#include "signal_fusion.h"
#include "seizure_model.h"
#include <stdbool.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "max30102.h"
#include "MPU6050.h"
#include "MessageQueue.h"

static const char *SF_TAG = "SignalFusion";

/* ------- tunables ------- */
#define SIGNAL_FUSION_HR_MIN       40      /* valid HR lower bound (bpm) */
#define SIGNAL_FUSION_HR_MAX       200     /* valid HR upper bound (bpm) */
#define SIGNAL_FUSION_KF_PROCESS_Q 0.5f    /* process variance per frame */
#define SIGNAL_FUSION_KF_BASE_R    9.0f    /* measurement noise at rest */
#define SIGNAL_FUSION_KF_MOTION_GAIN 60.0f /* R = BASE + GAIN*motion */
#define SIGNAL_FUSION_KF_MOTION_MAX  1.0f  /* block update when motion >= this */
#define SIGNAL_FUSION_KF_HOLD_LIMIT  8     /* consecutive invalid frames before invalid out */

static float   s_kf_hr = 70.0f;
static float   s_kf_p  = 30.0f;
static uint8_t s_kf_hold = 0;
static bool    s_kf_inited = false;

/* ------- 癫痫模型检测（20Hz 采样，200 帧 = 10s 窗口） ------- */
#define SEIZURE_SAMPLE_MS      50
#define SEIZURE_ACC_LSB_PER_G  2048.0f   /* MPU6050 ±16g: 2048 LSB/g */
#define SEIZURE_SPO2_LOW       90.0f
#define SEIZURE_THRESHOLD      0.81f
#define SEIZURE_TEMP_REFRESH_MS 3000

static float     s_seiz_frames[SEIZURE_MODEL_FRAMES][SEIZURE_MODEL_CHANNELS];
static uint32_t  s_seiz_idx = 0;
static float     s_seiz_prob = 0.0f;
static uint8_t   s_seiz_high = 0;
static uint8_t   s_seiz_window_filled = 0;
static uint32_t  s_seiz_windows = 0;
static uint32_t  s_seiz_spo2 = 0;
static float     s_seiz_temp = 30.0f;
static uint32_t  s_seiz_temp_ms = 0;
static bool      s_seiz_task_created = false;
static TaskHandle_t s_seiz_task      = NULL;

static void Task_Seizure_Detector(void *pv);

void SignalFusion_Init(void)
{
    s_kf_hr = 70.0f;
    s_kf_p  = 30.0f;
    s_kf_hold = 0;
    s_kf_inited = false;

    /* 癫痫模型：不在此处加载模型（调用方 main 任务栈太小）。
     * 由 Task_Seizure_Detector 在自己的大任务栈中执行 SeizureModel_Init()。 */
    if (!s_seiz_task_created) {
        s_seiz_task_created = true;
        xTaskCreatePinnedToCore(Task_Seizure_Detector, "Seizure_Det",
                                16384, NULL, 3, &s_seiz_task, 1);
    }
}

void SignalFusion_Stop(void)
{
    /* 停止融合+检测任务（进入深度睡眠/关机前调用） */
    if (s_seiz_task) {
        vTaskDelete(s_seiz_task);
        s_seiz_task = NULL;
    }
    s_seiz_task_created = false;
}

void SignalFusion_Hr_Update(int32_t hr_raw, uint8_t hr_raw_valid, float motion_index,
                            int32_t *hr_out, uint8_t *hr_valid_out)
{
    if (motion_index < 0.0f) motion_index = 0.0f;

    if (!s_kf_inited) {
        s_kf_hr = hr_raw_valid ? (float)hr_raw : 70.0f;
        s_kf_p  = 30.0f;
        s_kf_hold = 0;
        s_kf_inited = true;
        *hr_out = (int32_t)(s_kf_hr + 0.5f);
        *hr_valid_out = hr_raw_valid ? 1 : 0;
        return;
    }

    s_kf_p += SIGNAL_FUSION_KF_PROCESS_Q;

    if (hr_raw_valid && motion_index < SIGNAL_FUSION_KF_MOTION_MAX) {
        float R = SIGNAL_FUSION_KF_BASE_R + SIGNAL_FUSION_KF_MOTION_GAIN * motion_index;
        float K = s_kf_p / (s_kf_p + R);
        s_kf_hr += K * ((float)hr_raw - s_kf_hr);
        s_kf_p *= (1.0f - K);
        s_kf_hold = 0;
    } else {
        if (s_kf_hold < SIGNAL_FUSION_KF_HOLD_LIMIT) s_kf_hold++;
    }

    if (s_kf_hr < (float)SIGNAL_FUSION_HR_MIN) s_kf_hr = (float)SIGNAL_FUSION_HR_MIN;
    if (s_kf_hr > (float)SIGNAL_FUSION_HR_MAX) s_kf_hr = (float)SIGNAL_FUSION_HR_MAX;

    *hr_valid_out = (s_kf_hold < SIGNAL_FUSION_KF_HOLD_LIMIT) ? 1 : 0;
    *hr_out = (int32_t)(s_kf_hr + 0.5f);
}

static void Task_Seizure_Detector(void *pv)
{
    /* TFLite-Micro 初始化/AllocateTensors 需要大栈，
     * 在自有任务栈里执行，避免爆掉调用方任务栈 */
    SeizureModel_Init();

    while (1) {
        int16_t ax = 0, ay = 0, az = 0;
        uint32_t ir = 0, red = 0;

        /* ---- Kalman HR fusion (moved from main.c Task_SignalFusion) ---- */
        int32_t hr_raw  = Max30102_Get_Raw_Heart_Rate();
        uint8_t hr_ok   = Max30102_Get_Raw_Heart_Rate_Valid();
        float   motion  = Mpu6050_Get_Motion_Index();
        int32_t hr_out  = 0;
        uint8_t hr_valid_out = 0;
        SignalFusion_Hr_Update(hr_raw, hr_ok, motion, &hr_out, &hr_valid_out);
        Max30102_Set_Fused_Heart_Rate(hr_out, hr_valid_out);

        Mpu6050_Get_Accel_Data(&ax, &ay, &az);
        Max30102_Get_Last_Raw(&ir, &red);

        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (now_ms - s_seiz_temp_ms >= SEIZURE_TEMP_REFRESH_MS) {
            s_seiz_temp_ms = now_ms;
            float t = Max30102_Get_Temperature_C();
            if (t > -20.0f && t < 60.0f) s_seiz_temp = t;
        }

        int32_t hr = Max30102_Get_Fused_Heart_Rate();
        if (hr <= 0) hr = 70;
        s_seiz_spo2 = (uint32_t)Max30102_Get_Raw_SpO2();

        float *fr = s_seiz_frames[s_seiz_idx];
        fr[0] = (float)ax / SEIZURE_ACC_LSB_PER_G;
        fr[1] = (float)ay / SEIZURE_ACC_LSB_PER_G;
        fr[2] = (float)az / SEIZURE_ACC_LSB_PER_G;
        fr[3] = (float)ir;
        fr[4] = (float)red;
        fr[5] = s_seiz_temp;
        fr[6] = (float)hr;
        s_seiz_idx++;

        if (s_seiz_idx >= SEIZURE_MODEL_FRAMES) {
            s_seiz_idx = 0;
            if (!SeizureModel_Is_Ready()) SeizureModel_Init();
            float prob = 0.0f;
            if (SeizureModel_Predict(s_seiz_frames, &prob)) {
                s_seiz_prob = prob;
                uint8_t h = (prob > SEIZURE_THRESHOLD) ? 1 : 0;
                if (s_seiz_spo2 >= 1 && (float)s_seiz_spo2 < SEIZURE_SPO2_LOW) h = 1;
                s_seiz_high = h;
                s_seiz_windows++;
                s_seiz_window_filled = 1;
                ESP_LOGI(SF_TAG, "癫痫推理 prob=%.3f spo2=%lu high_risk=%u",
                         prob, (unsigned long)s_seiz_spo2, h);
                /* 模型结果投递给 MQTT 发送任务（OneNET 四信息点上报） */
                Message_Queue_Send_Seizure_Model(prob,
                                                 (uint32_t)hr,
                                                 s_seiz_spo2,
                                                 Get_isFall());
            } else {
                ESP_LOGW(SF_TAG, "癫痫推理失败/模型未就绪");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(SEIZURE_SAMPLE_MS));
    }
}

void SignalFusion_Seizure_Get(SignalFusion_Seizure_Result_t *out)
{
    if (!out) return;
    out->seizure_prob  = s_seiz_prob;
    out->high_risk     = s_seiz_high;
    out->model_ready   = SeizureModel_Is_Ready() ? 1 : 0;
    out->window_filled = s_seiz_window_filled;
    out->windows_done  = s_seiz_windows;
    out->spo2          = s_seiz_spo2;
}