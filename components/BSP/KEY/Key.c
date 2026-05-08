#include "Key.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_timer.h"

static const char *TAG = "KEY";

typedef enum {
    STATE_IDLE,
    STATE_DEBOUNCE,
    STATE_WAIT_RELEASE,
    STATE_RELEASE_WAITED,
} key_state_t;

typedef struct {
    gpio_num_t    gpio;
    TimerHandle_t timer;

    key_state_t   state;
    uint32_t      press_tick;   // 按下时刻

    volatile key_event_t event_flag; // 供轮询模式使用
} key_ctx_t;

static key_ctx_t s_keys[KEY_NUM] = {
    [0] = { .gpio = KEY_GPIO_1, .state = STATE_IDLE },
};

static key_callback_t s_user_cb = NULL;
static portMUX_TYPE s_key_mux = portMUX_INITIALIZER_UNLOCKED;

// 触发回调或设置标志
static void trigger_event(key_ctx_t *ctx, key_event_t event) {
    key_id_t id = (key_id_t)((ctx - s_keys) + 1);
    if (s_user_cb) {
        s_user_cb(id, event);
    } else {
        portENTER_CRITICAL(&s_key_mux);
        ctx->event_flag = event;
        portEXIT_CRITICAL(&s_key_mux);
    }
}

static void IRAM_ATTR key_isr_handler(void *arg)
{
    key_ctx_t *ctx = (key_ctx_t *)arg;
    BaseType_t higher_woken = pdFALSE;
    // 只要有电平变化，就重置定时器（重新触发状态检查）
    xTimerResetFromISR(ctx->timer, &higher_woken);
    if (higher_woken) portYIELD_FROM_ISR();
}

static void key_timer_cb(TimerHandle_t timer)
{
    key_ctx_t *ctx = (key_ctx_t *)pvTimerGetTimerID(timer);
    int level = gpio_get_level(ctx->gpio);
    uint32_t now = xTaskGetTickCount();

    switch (ctx->state) {
        case STATE_IDLE:
            if (level == 0) { 
                ctx->state = STATE_DEBOUNCE;
                ctx->press_tick = now;
                xTimerChangePeriod(ctx->timer, pdMS_TO_TICKS(KEY_DEBOUNCE_MS), 0);
            }
            break;

        case STATE_DEBOUNCE:
            if (level == 0) { 
                ctx->state = STATE_WAIT_RELEASE;
                // 开始长按监视
                xTimerChangePeriod(ctx->timer, pdMS_TO_TICKS(KEY_LONG_MS), 0);
            } else {
                ctx->state = STATE_IDLE;
            }
            break;

        case STATE_WAIT_RELEASE:
            if (level == 0) { // 手指还没抬起来
                if ((now - ctx->press_tick) >= pdMS_TO_TICKS(KEY_LONG_MS)) {
                    trigger_event(ctx, KEY_EVENT_LONG_PRESS);
                    // 【关键修改】：进入一个死等松开的状态，防止重复触发
                    ctx->state = STATE_RELEASE_WAITED;
                    xTimerChangePeriod(ctx->timer, pdMS_TO_TICKS(50), 0); // 抬手检查频率
                }
            } else { // 提前松开了 -> 判定为单击
                trigger_event(ctx, KEY_EVENT_SINGLE_CLICK);
                ctx->state = STATE_IDLE;
                xTimerStop(ctx->timer, 0); // 停止长按定时器，防止继续检测长按
            }
            break;

        case STATE_RELEASE_WAITED: // 新增状态
            if (level == 1) { // 终于松手了
                ctx->state = STATE_IDLE;
            }
            break;
    }
}

void Key_Init(key_callback_t cb)
{
    s_user_cb = cb;
    for (int i = 0; i < KEY_NUM; i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << s_keys[i].gpio,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .intr_type    = GPIO_INTR_ANYEDGE, // ✅ 改为任意沿触发
        };
        gpio_config(&cfg);

        s_keys[i].timer = xTimerCreate(
            "key_tmr",
            pdMS_TO_TICKS(KEY_DEBOUNCE_MS),
            pdFALSE,
            (void *)&s_keys[i],
            key_timer_cb
        );

        gpio_isr_handler_add(s_keys[i].gpio, key_isr_handler, (void *)&s_keys[i]);
    }
    ESP_LOGI(TAG, "Key Advanced Init OK");
}

key_result_t Key_Get(void)
{
    key_result_t res = { .id = KEY_NONE, .event = KEY_EVENT_NONE };
    for (int i = 0; i < KEY_NUM; i++) {
        if (s_keys[i].event_flag != KEY_EVENT_NONE) {
            portENTER_CRITICAL(&s_key_mux);
            res.id = (key_id_t)(i + 1);
            res.event = s_keys[i].event_flag;
            s_keys[i].event_flag = KEY_EVENT_NONE;
            portEXIT_CRITICAL(&s_key_mux);
            return res;
        }
    }
    return res;
}