#include "Key.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_timer.h"

static const char *TAG = "KEY";

typedef enum {
    STATE_IDLE,
    STATE_DEBOUNCE,
    STATE_WAIT_RELEASE,
    STATE_WAIT_DOUBLE,
} key_state_t;

typedef struct {
    gpio_num_t    gpio;
    TimerHandle_t timer;
    
    key_state_t   state;
    uint32_t      press_tick;   // 按下时刻
    uint8_t       click_cnt;    // 点击计数
    
    volatile key_event_t event_flag; // 供轮询模式使用
} key_ctx_t;

static key_ctx_t s_keys[KEY_NUM] = {
    [0] = { .gpio = KEY_GPIO_1, .state = STATE_IDLE },
    [1] = { .gpio = KEY_GPIO_2, .state = STATE_IDLE },
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
            if (level == 0) { // 检测到按下
                ctx->state = STATE_DEBOUNCE;
                ctx->press_tick = now;
                xTimerChangePeriod(ctx->timer, pdMS_TO_TICKS(KEY_DEBOUNCE_MS), 0);
            }
            break;

        case STATE_DEBOUNCE:
            if (level == 0) { // 消抖通过，确实按下了
                ctx->state = STATE_WAIT_RELEASE;
                // 设置定时器在长按阈值后唤醒，用于检测长按
                xTimerChangePeriod(ctx->timer, pdMS_TO_TICKS(KEY_LONG_MS), 0);
            } else {
                ctx->state = STATE_IDLE;
            }
            break;

        case STATE_WAIT_RELEASE:
            if (level == 0) { // 仍然按着
                if ((now - ctx->press_tick) >= pdMS_TO_TICKS(KEY_LONG_MS)) {
                    trigger_event(ctx, KEY_EVENT_LONG_PRESS);
                    ctx->state = STATE_IDLE; // 长按触发后需松开才能下一次
                }
            } else { // 已松开
                ctx->click_cnt++;
                ctx->state = STATE_WAIT_DOUBLE;
                // 等待一段时间看是否有第二次点击
                xTimerChangePeriod(ctx->timer, pdMS_TO_TICKS(KEY_DOUBLE_MS), 0);
            }
            break;

        case STATE_WAIT_DOUBLE:
            if (level == 0) { // 在有效时间内再次按下
                ctx->click_cnt++;
                ctx->state = STATE_WAIT_RELEASE;
                xTimerChangePeriod(ctx->timer, pdMS_TO_TICKS(KEY_LONG_MS), 0);
            } else { // 超时未再次按下
                if (ctx->click_cnt >= 2) {
                    trigger_event(ctx, KEY_EVENT_DOUBLE_CLICK);
                } else if (ctx->click_cnt == 1) {
                    trigger_event(ctx, KEY_EVENT_SINGLE_CLICK);
                }
                ctx->click_cnt = 0;
                ctx->state = STATE_IDLE;
            }
            break;
    }
}

void Key_Init(key_callback_t cb)
{
    s_user_cb = cb;

    gpio_install_isr_service(0); // 确保驱动已安装

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