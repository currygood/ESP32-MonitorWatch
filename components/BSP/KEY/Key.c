#include "Key.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_timer.h"

static const char *TAG = "KEY";

typedef enum {
    STATE_IDLE,
    STATE_DEBOUNCE,
    STATE_WAIT_RELEASE,
	STATE_WAIT_FOR_SECOND_PRESS, // 新增：等待第二次按下的窗口期
    STATE_DEBOUNCE_SECOND,       // 新增：第二次按下的消抖
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
	[1] = { .gpio = KEY_GPIO_2, .state = STATE_IDLE },
	[2] = { .gpio = KEY_GPIO_3, .state = STATE_IDLE },
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
    // 防止在 STATE_WAIT_RELEASE（周期=1000ms）时，松手的上升沿
    // 被 Reset 成继续等 1000ms 才回调的问题
    xTimerChangePeriodFromISR(ctx->timer, pdMS_TO_TICKS(KEY_DEBOUNCE_MS), &higher_woken);
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
                    ctx->state = STATE_RELEASE_WAITED;
                    xTimerChangePeriod(ctx->timer, pdMS_TO_TICKS(50), 0);
                }
            } else { // 第一次松开了！
                // 不要触发单击，进入双击等待窗口
                ctx->state = STATE_WAIT_FOR_SECOND_PRESS;
                xTimerChangePeriod(ctx->timer, pdMS_TO_TICKS(KEY_DOUBLE_MS), 0);
            }
            break;

        case STATE_WAIT_FOR_SECOND_PRESS:
            if (level == 0) { // 在窗口期内再次按下
                ctx->state = STATE_DEBOUNCE_SECOND;
                xTimerChangePeriod(ctx->timer, pdMS_TO_TICKS(KEY_DEBOUNCE_MS), 0);
            } else { 
                // 窗口期到了（定时器溢出），依然是高电平 -> 判定为单击
                trigger_event(ctx, KEY_EVENT_SINGLE_CLICK);
                ctx->state = STATE_IDLE;
                xTimerStop(ctx->timer, 0);
            }
            break;

        case STATE_DEBOUNCE_SECOND:
            if (level == 0) { // 确认是第二次按下
                trigger_event(ctx, KEY_EVENT_DOUBLE_CLICK);
                ctx->state = STATE_RELEASE_WAITED;
                xTimerChangePeriod(ctx->timer, pdMS_TO_TICKS(50), 0);
            } else {
                // 可能是抖动，超时后判定为之前的单击
                trigger_event(ctx, KEY_EVENT_SINGLE_CLICK);
                ctx->state = STATE_IDLE;
            }
            break;

        case STATE_RELEASE_WAITED:
            if (level == 1) { 
                ctx->state = STATE_IDLE;
                xTimerStop(ctx->timer, 0);
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