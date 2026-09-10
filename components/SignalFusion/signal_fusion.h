#ifndef SIGNAL_FUSION_H
#define SIGNAL_FUSION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reset Kalman state (call once before first use). */
void SignalFusion_Init(void);

/* Motion-weighted Kalman HR fusion.
 * hr_raw:       raw HR (bpm) from the sensor algorithm
 * hr_raw_valid: 1 if the raw measurement passed plausibility
 * motion_index: 0 = still, bigger = more motion (MPU6050 |acc-1g| EMA)
 * hr_out:       fused HR (always written)
 * hr_valid_out: 1 only when fused output is trustworthy (freeze+hold)
 */
void SignalFusion_Hr_Update(int32_t hr_raw, uint8_t hr_raw_valid, float motion_index,
                            int32_t *hr_out, uint8_t *hr_valid_out);

/* --- 癫痫发作模型检测（SignalFusion_Init 后自动运行） --- */
typedef struct {
    float    seizure_prob;   /* 最近一个 10s 窗口的发作概率 [0,1] */
    uint8_t  high_risk;      /* 发作或 SpO2<90% 的高风险标志 */
    uint8_t  model_ready;    /* model 分区加载成功 */
    uint8_t  window_filled;  /* 是否已产出第一个窗口结果 */
    uint32_t windows_done;   /* 累计推理窗口数 */
    uint32_t spo2;           /* 最近一次 SpO2 (%)，0=无效 */
} SignalFusion_Seizure_Result_t;

void SignalFusion_Seizure_Get(SignalFusion_Seizure_Result_t *out);

/* Stop the sampling/inference task (call before deep sleep / power off). */
void SignalFusion_Stop(void);

#ifdef __cplusplus
}
#endif

#endif // SIGNAL_FUSION_H