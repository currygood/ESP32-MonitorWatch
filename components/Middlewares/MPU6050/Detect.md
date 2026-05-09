现在的检测算法不够敏感，主要原因有两个：
窗口期太长：你的 MPU6050_BUFFER_SIZE 是 200，在 50Hz 下是 4秒钟 的数据。如果你只摇了 0.5 秒，这 0.5 秒的高能量会被剩下 3.5 秒的静止数据“稀释”掉，导致平均值（avg_mag）拉不起来。
算法过于简单：只看了最大值和平均值，没有看“动作的激烈程度”（即加速度的变化率）。
为了提高灵敏度，我们需要引入 “活动能量（Activity Index）” 的概念，并对 “跌倒（撞击）” 和 “抽搐（持续震动）” 分开判断。
改进后的检测算法（MPU6050.c）
请尝试替换原有的 Mpu6050_Detect_Fall_Or_Convulsion 函数：
code
C
bool Mpu6050_Detect_Fall_Or_Convulsion(int16_t *ax_buf, int16_t *ay_buf, int16_t *az_buf, int len) {
    if (len < 10) return false;

    float max_mag = 0;          // 最大震幅
    float total_variation = 0;  // 总能量变化（用于检测抽搐）
    int high_peak_count = 0;    // 高频峰值计数
    float last_mag = 1.0f;

    for (int i = 0; i < len; i++) {
        // 计算当前g值 (16g量程，Lsb=2048)
        float mag = sqrtf((float)ax_buf[i]*ax_buf[i] +
                          (float)ay_buf[i]*ay_buf[i] +
                          (float)az_buf[i]*az_buf[i]) / 2048.0f;

        // 1. 记录最大撞击力
        if (mag > max_mag) max_mag = mag;

        // 2. 计算活动能量 (当前幅值与上一个采样点的差的绝对值)
        total_variation += fabsf(mag - last_mag);
        
        // 3. 统计显著活跃点 (大于1.8g视为活跃)
        if (mag > 1.8f) high_peak_count++;

        last_mag = mag;
    }

    // 归一化活跃能量
    float activity_score = total_variation / len;

    // --- 逻辑判断 ---

    // A. 跌倒检测逻辑：瞬间撞击力很大 (阈值从2.2调低到2.0)
    if (max_mag > 2.0f && activity_score > 0.15f) {
        ESP_LOGW(TAG, "🔔 检测到跌倒/撞击! MaxMag:%.2f G, Score:%.2f", max_mag, activity_score);
        return true;
    }

    // B. 抽搐检测逻辑：不需要单次撞击力很大，但需要持续的高频震动
    // activity_score > 0.25 说明一直在剧烈摇晃
    // high_peak_count > 15 说明在4秒窗口内有很多次加速动作
    if (activity_score > 0.25f || (high_peak_count > 15 && activity_score > 0.2f)) {
        ESP_LOGW(TAG, "🚨 检测到持续抽搐摇晃! Score:%.2f, Peaks:%d", activity_score, high_peak_count);
        return true;
    }

    return false;
}
为什么这个更灵敏？
引入 activity_score：它计算的是加速度计数值的“抖动幅度”。静止不动时，这个分值接近 0；正常走路约为 0.05-0.1；当你用力摇晃时，这个分值会迅速飙升到 0.3 以上。
high_peak_count 峰值计数：抽搐的特点是反复的高频震动。即使单次撞击没达到跌倒的程度，但如果 4 秒内出现了几十次超过 1.8g 的加速度，那一定是异常摇晃。
分流判断：
跌倒：看重 max_mag（有没有砸在那一下）。
抽搐：看重 activity_score（是不是一直在疯狂抖动）。
进一步调试建议：
如果修改后仍然觉得不够灵敏，可以调整以下参数：
减小缓冲区：在 MPU6050.h 中将 MPU6050_BUFFER_SIZE 从 200 改为 100（即 2 秒更新一次）。这样由于分母变小，短时间的摇晃更容易被识别。
降低 high_peak_count 阈值：将 15 改为 10。
降低 activity_score 阈值：将 0.25f 改为 0.18f。
注意：灵敏度调得越高，误报（比如甩个手、跑个步被误认为癫痫）的概率就越高。建议你在串口监视器里观察你正常摇晃时的 Score 打印值，找一个刚好能触发的临界点。