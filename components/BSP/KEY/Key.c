#include "Key.h"
#include "esp_log.h"
#include "esp_attr.h"

static const char *TAG = "KEY";

typedef struct {
    gpio_num_t    gpio;
    TimerHandle_t timer;
    volatile uint8_t flag;
} key_ctx_t;

static key_ctx_t s_keys[KEY_NUM] = {
    [0] = { .gpio = KEY_GPIO_1, .flag = 0 },
    [1] = { .gpio = KEY_GPIO_2, .flag = 0 },
};

static key_callback_t s_user_cb = NULL;

// ✅ 声明自旋锁，不能传 NULL！
static portMUX_TYPE s_key_mux = portMUX_INITIALIZER_UNLOCKED;

static void IRAM_ATTR key_isr_handler(void *arg)
{
    key_ctx_t *ctx = (key_ctx_t *)arg;
    BaseType_t higher_woken = pdFALSE;
    xTimerResetFromISR(ctx->timer, &higher_woken);
    if (higher_woken) portYIELD_FROM_ISR();
}

static void key_timer_cb(TimerHandle_t timer)
{
    key_ctx_t *ctx = (key_ctx_t *)pvTimerGetTimerID(timer);
    
    // 消抖：定时器结束后再次确认电平
    if (gpio_get_level(ctx->gpio) == 0) { 
        key_id_t id = (key_id_t)((ctx - s_keys) + 1);
        if (s_user_cb) {
            s_user_cb(id);
        } else {
            portENTER_CRITICAL(&s_key_mux);
            ctx->flag = 1;
            portEXIT_CRITICAL(&s_key_mux);
        }
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
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_NEGEDGE,
        };
        ESP_ERROR_CHECK(gpio_config(&cfg));
    }

    for (int i = 0; i < KEY_NUM; i++) {
        s_keys[i].timer = xTimerCreate(
            "key_debounce",
            pdMS_TO_TICKS(KEY_DEBOUNCE_MS),
            pdFALSE,
            (void *)&s_keys[i],
            key_timer_cb
        );
        if (!s_keys[i].timer) {
            ESP_LOGE(TAG, "Timer create failed for key %d", i);
            return;
        }
    }

    for (int i = 0; i < KEY_NUM; i++) {
        ESP_ERROR_CHECK(
            gpio_isr_handler_add(s_keys[i].gpio, key_isr_handler, (void *)&s_keys[i])
        );
    }
    ESP_LOGI(TAG, "Key init OK");
}

key_id_t Key_Get(void)
{
    for (int i = 0; i < KEY_NUM; i++) {
        if (s_keys[i].flag) {
            portENTER_CRITICAL(&s_key_mux);  // ✅ 传锁地址
            s_keys[i].flag = 0;
            portEXIT_CRITICAL(&s_key_mux);
            return (key_id_t)(i + 1);
        }
    }
    return KEY_NONE;
}