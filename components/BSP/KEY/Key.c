#include "Key.h"
#include "esp_log.h"

static const char *TAG = "KEY";

typedef enum {
    STATE_IDLE,
    STATE_DEBOUNCE,
    STATE_WAIT_RELEASE,
    STATE_WAIT_DOUBLE,
    STATE_WAIT_RELEASE_FINAL // 锁死状态
} key_state_t;

typedef struct {
    gpio_num_t gpio;
    TimerHandle_t timer;
    key_state_t state;
    uint32_t press_tick;
    uint8_t click_cnt;
} key_ctx_t;

static key_ctx_t s_keys[KEY_NUM] = {
    [0] = { .gpio = KEY_GPIO_1, .state = STATE_IDLE },
    [1] = { .gpio = KEY_GPIO_2, .state = STATE_IDLE },
};

static QueueHandle_t s_key_queue = NULL;

static void trigger_event(key_ctx_t *ctx, key_event_t event) {
    key_result_t res = { .id = (key_id_t)((ctx - s_keys) + 1), .event = event };
    if (s_key_queue) xQueueSend(s_key_queue, &res, 0);
}

static void IRAM_ATTR key_isr_handler(void *arg) {
    key_ctx_t *ctx = (key_ctx_t *)arg;
    BaseType_t woken = pdFALSE;
    xTimerResetFromISR(ctx->timer, &woken);
    if (woken) portYIELD_FROM_ISR();
}

static void key_timer_cb(TimerHandle_t timer) {
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
                xTimerChangePeriod(ctx->timer, pdMS_TO_TICKS(KEY_LONG_MS), 0);
            } else ctx->state = STATE_IDLE;
            break;
        case STATE_WAIT_RELEASE:
            if (level == 0) {
                if ((now - ctx->press_tick) >= pdMS_TO_TICKS(KEY_LONG_MS)) {
                    trigger_event(ctx, KEY_EVENT_LONG_PRESS);
                    ctx->state = STATE_WAIT_RELEASE_FINAL; // 锁死，防重复触发
                }
            } else {
                ctx->click_cnt++;
                ctx->state = STATE_WAIT_DOUBLE;
                xTimerChangePeriod(ctx->timer, pdMS_TO_TICKS(KEY_DOUBLE_MS), 0);
            }
            break;
        case STATE_WAIT_DOUBLE:
            if (level == 0) { // 第二次按下了
                ctx->state = STATE_WAIT_RELEASE;
                xTimerChangePeriod(ctx->timer, pdMS_TO_TICKS(KEY_LONG_MS), 0);
            } else {
                trigger_event(ctx, (ctx->click_cnt >= 2) ? KEY_EVENT_DOUBLE_CLICK : KEY_EVENT_SINGLE_CLICK);
                ctx->click_cnt = 0;
                ctx->state = STATE_IDLE;
            }
            break;
        case STATE_WAIT_RELEASE_FINAL:
            if (level != 0) ctx->state = STATE_IDLE; // 物理松开后才回空闲
            break;
    }
}

void Key_Init(void) {
    if (!s_key_queue) s_key_queue = xQueueCreate(10, sizeof(key_result_t));
    
    // 注意：这里绝对不要再调用 gpio_install_isr_service，main.c里已经有了
    for (int i = 0; i < KEY_NUM; i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << s_keys[i].gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .intr_type = GPIO_INTR_ANYEDGE,
        };
        gpio_config(&cfg);
        s_keys[i].timer = xTimerCreate("k", pdMS_TO_TICKS(KEY_DEBOUNCE_MS), pdFALSE, &s_keys[i], key_timer_cb);
        gpio_isr_handler_add(s_keys[i].gpio, key_isr_handler, &s_keys[i]);
    }
}

bool Key_Get_Event(key_result_t *res, uint32_t wait_ms) {
    return xQueueReceive(s_key_queue, res, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
}

