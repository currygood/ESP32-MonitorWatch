# ESP32-S3 端侧癫痫检测模型 —— 训练与推理说明

本文档记录端侧癫痫发作检测模型的完整链路：数据准备 → 预处理 → 模型训练 → INT8 量化导出 → ESP32-S3 固件推理与发病判定。

## 1. 任务定义

- **输入**：10 秒生理信号窗口，7 通道特征
- **输出**：发作概率 P ∈ [0,1] → 发病判定（是/否）＋ 风险等级（0~100）
- **模型**：1D-CNN 二分类（正常/发作），TFLite-Micro 端侧推理，运行在 ESP32-S3 上
- **载体**：SignalFusion 模块负责采样、滤波、特征构建与推理调度，MPU6050 / MAX30102 仅提供原始数据，不做癫痫判断

## 2. 数据准备与标签

每窗 10 秒、100Hz，统一 9 列 CSV：`t_rel_ms, ax_g, ay_g, az_g, ppg_ir, ppg_red, temp_c, hr_bpm, label`（实际训练只用后 7 列特征 + label）。

| 项目 | 数值 |
| --- | --- |
| 窗口总数 | 1212（正常 1002 / 发作 210，正类占比 17.3%） |
| 划分 | train 836 / val 210 / test 166（按文件确定性哈希） |
| 标签定义 | label=0 正常；label=1 发作 |
| 类别不平衡 | class_weight {0: 0.61, 1: 2.73} 加权损失 |

## 3. 特征与预处理

训练使用 **7 个通道**（也就是模型最终需要的输入参数）：

| # | 通道 | 含义 | 单位/量纲 |
| --- | --- | --- | --- |
| 1 | ax_g | 加速度 X 轴（归一化到 g） | g（±16g 量程，2048 LSB/g） |
| 2 | ay_g | 加速度 Y 轴 | g |
| 3 | az_g | 加速度 Z 轴 | g |
| 4 | ppg_ir | 红外 PPG 原始 | ADC 原始整数值 |
| 5 | ppg_red | 红色 PPG 原始 | ADC 原始整数值 |
| 6 | temp_c | 温度 | ℃ |
| 7 | hr_bpm | 心率（经卡尔曼融合） | bpm |

预处理流程（训练与推理完全一致）：

1. **窗口化**：10s 固定长度，100Hz 采样得 1000 点
2. **降采样**：每 5 点取均值 → 200 帧（等效 20Hz）
3. **缺值填充**：个别窗口有 NaN（如 HR 缺失），按训练集逐通道均值填充，再走降采样
4. **标准化**：逐通道 `(x - mean) / std`，mean/std 仅从**训练集**统计，避免数据泄漏

## 4. 为什么用 z-score 标准化，而不是 min-max 归一化

先澄清术语：本项目实际执行的变换是 `(x - mean) / std`（训练脚本与固件代码一致，仅此一种），其严格数学名称为 **z-score 标准化**（均值 0、方差 1）。本仓库 `TrainData/ESP32Watch` 的 README 与代码注释中将其简称为"归一化"（"归一化"在此是"让特征可比较的预处理"的泛称）；真正把数据压到 [0,1] 的 min-max 归一化本项目并未使用。之所以**不用 min-max 归一化**，原因如下：

| 对比项 | min-max 归一化 (x - min)/(max - min) | z-score 标准化 (x - mean)/std |
| --- | --- | --- |
| 对离群点敏感度 | 高：一个毛刺尖峰就会把 max 拉爆，其余数据被压扁 | 低：均值/标准差由整体分布决定，单点尖峰影响小 |
| 特征中心 | 中心依赖 min/max，不固定 | 中心恒为 0，配合 sigmoid 输出与偏置初始化更容易收敛 |
| 多通道量纲差异 | PPG ADC 值上万、加速度才 ±3g，min-max 会把小量纲通道细节淹没 | 通道都被压缩到同一量纲（约 ±3σ），权重学习公平 |
| 推理端实现 | 需要缓存 min/max（还要处理极值漂移） | 只需 7 个 mean + 7 个 std 常数，固件一行除法即可复现 |
| INT8 量化友好度 | 分布可能偏斜，量化动态范围大 | 数据集中在 ±3，量化误差更小 |

结论：考虑到 PPG 原始信号会叠加运动尖峰值，min-max 会被极值绑架，z-score 更鲁棒、等价实现也更简单——这正是 `model/normalization.json`（7 通道 mean/std）存在的原因。

## 5. 模型结构（1D-CNN）

| 层 | 输出形状 | 说明 |
| --- | --- | --- |
| InputLayer | [200, 7] | 200 帧 × 7 通道 |
| Conv1D(8, k=5, stride=2, ReLU) | [100, 8] | 捕捉短时波形特征 |
| MaxPool1D(2) | [50, 8] | 下采样 |
| Conv1D(16, k=3, stride=2, ReLU) | [25, 16] | 更深层特征 |
| GlobalAveragePooling1D | [16] | 全局池化，消除大量全连接参数 |
| Dense(16, ReLU) | [16] | 特征组合 |
| Dense(1, Sigmoid) | [1] | 输出发作概率 |

参数量很小（量化后 8.4 KB），足以在 ESP32-S3 上流畅运行。

## 6. 训练配置

- 优化器：Adam（lr=1e-3）
- 损失：binary_crossentropy（二分类）
- 监控指标：accuracy
- 训练轮数：40（上限），EarlyStopping(monitor=val_loss, patience=8, restore_best_weights=True)
- batch_size：32
- 随机种子：7（可复现）
- 类别不平衡：class_weight {0: 0.61, 1: 2.73}，避免"全判正常"的假高精度
- 训练环境：TensorFlow 2.21，脚本 `scripts/06_train_export_esp32.py` 一键完成 训练→评估→量化→导出

## 7. 量化与导出

- 量化方式：INT8 全量化（权重 + 激活），用 300 个训练窗口做 representative dataset 标定缩放参数
- 输入/输出保持 **float32**（INT8 只用内部张量）：固件直接喂浮点特征，无需转 int8，精度损失更小
- 产物（models/ 目录）：

| 文件 | 说明 |
| --- | --- |
| model_int8.tflite（8.4 KB） | 烧录到 `model` SPIFFS 分区（0xF00000 / 1MB），推荐 |
| model_float32.tflite（9.0 KB） | 全浮点对照版 |
| seizure_model_int8.h | C 数组版（嵌入编译用） |
| normalization.json | 7 通道 mean/std，固件加载 |
| metrics.json | 训练指标 |

## 8. 输入进模型的东西：一次推理喂多少参数

一次推理喂入的是一整张窗口张量：**形状 [1, 200, 7] = 1400 个 float32 值**（约 5.6 KB 内存），即 10 秒内采到的 200 帧 × 7 通道。

固件采帧逻辑（SignalFusion）：

```c
// 每 50 ms 一帧（20Hz），攒满 200 帧 = 10 秒窗口后推理
fr[0] = ax / 2048.0f;   // MPU6050 加速度 → g
fr[1] = ay / 2048.0f;
fr[2] = az / 2048.0f;
fr[3] = (float)ir;      // PPG 红外原始
fr[4] = (float)red;     // PPG 红色原始
fr[5] = s_seiz_temp;    // 温度（3s 刷新一次）
fr[6] = (float)hr;      // 卡尔曼融合心率
```

攒满 200 帧 → 逐通道 z-score 归一化 → 调用 `SeizureModel_Predict()`。

## 9. 模型的输出：不是 0/1，是一个概率

模型输出张量 **形状 [1, 1]，即一个 float32 概率值 P∈[0,1]**（Sigmoid 层），代表"这一窗口疑似发作"的置信度。

发病判定由业务层完成（`signal_fusion.c`）：

```c
#define SEIZURE_THRESHOLD 0.81f
uint8_t h = (prob > SEIZURE_THRESHOLD) ? 1 : 0;   // 模型输出概率大于 0.81 → 判为发病
```

- P > 0.81 → high_risk = 1（判发病）
- P ≤ 0.81 → high_risk = 0（不判发病）
- **SpO₂ < 90% 时强制 high_risk = 1**（血氧过低是独立于模型的报警规则，不参与模型输入）
- 风险等级：`seizure_risk_level = round(P × 100)`（0~100），SpO₂<90 时强制不低于 90

这个 0~100 的等级会和心率、血氧、异常运动标志一起，经 MQTT 一次上报给 OneNET：

| 信息点 | 类型 | 含义 |
| --- | --- | --- |
| abnormal_motion_detected | bool | 异常运动/跌倒标志 |
| heart_rate | number | 卡尔曼融合心率 |
| oxygen_saturation | number | 血氧饱和度 |
| seizure_risk_level | number | 癫痫发作风险 0~100 |

## 10. 训练结果

| 集合 | 数量 | acc | precision | recall | F1 |
| --- | --- | --- | --- | --- | --- |
| val | 210 | 1.0 | 1.0 | 1.0 | 1.0 |
| test | 166 | 1.0 | 1.0 | 1.0 | 1.0 |

提醒：训练/测试数据中"发作"样本主要由合成生理信号构成，与真实正常数据差别大，模型"轻松学会"两者区分（指标接近 1.0 说明分布可分性好，不代表真实现场能做到 100%）。**真机部署前务必用手表自身采集的真实数据复测**，用真实佩戴数据的表现才是最终可信指标。

## 11. 复现

依赖：TensorFlow 2.21（`F:\ModelTrain\tf-env` 已具备）。

```bash
python scripts/00_validate_raw.py       # 可选：校验原始数据
python scripts/01_slice_windows.py      # 真实数据 → 10s 窗口
python scripts/02_generate_synthetic_seizure.py  # 生成合成发作段
python scripts/03_build_manifest.py     # 汇总清单 + 划分
python scripts/05_export_train7.py      # 导出 7 通道训练集
python scripts/06_train_export_esp32.py # 训练 + 量化 + 导出全部产物
```

产物：[模型 + 参数 + 指标] 位于 `models/`，烧录与使用方式见第 7 节。

## 12. 已知限制与后续改进

1. 缺少真实癫痫患者腕部（IMU+PPG）数据，合成发作段与真实生理波形仍有差距
2. 阈值 0.81 目前是固定值，可收集真机数据后按 precision/recall 曲线重新标定
3. 血氧兜底规则（SpO₂<90）目前仅作为报警层附加规则，后续可以并入模型训练
4. 可尝试更轻量的单层 LSTM / 1D-CNN+注意力对比，进一步压缩到 5KB 以内
