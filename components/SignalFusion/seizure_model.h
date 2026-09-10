#ifndef SEIZURE_MODEL_H
#define SEIZURE_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEIZURE_MODEL_FRAMES   200
#define SEIZURE_MODEL_CHANNELS 7

/* 从 flash 的 model 分区加载 int8 TFLite 模型并初始化解释器（幂等，可重复调用）。 */
void SeizureModel_Init(void);

/* 模型与归一化参数是否成功加载。 */
bool SeizureModel_Is_Ready(void);

/* 输入 200x7 原始帧（ax,ay,az,ppg_ir,ppg_red,temp_c,hr_bpm），内部完成归一化，
 * 输出发作概率 [0,1]。成功返回 true。 */
bool SeizureModel_Predict(const float frames[SEIZURE_MODEL_FRAMES][SEIZURE_MODEL_CHANNELS],
                          float *prob_out);

#ifdef __cplusplus
}
#endif

#endif // SEIZURE_MODEL_H