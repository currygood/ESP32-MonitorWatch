#include "seizure_model.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <new>

#include "esp_log.h"
#include "esp_spiffs.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"

#define SEIZURE_TAG          "SeizureModel"
#define SEIZURE_MODEL_PATH   "/model/model_int8.tflite"
#define SEIZURE_JSON_PATH    "/model/normalization.json"
#define SEIZURE_ARENA_BYTES  (32 * 1024)

/* 回退归一化参数（与训练 normalization.json 一致） */
static const float fallback_mean[SEIZURE_MODEL_CHANNELS] = {
    0.479680f, 1.316833f, -1.766193f, 151237.390625f, 12951.662109f, 30.609297f, 89.938499f
};
static const float fallback_std[SEIZURE_MODEL_CHANNELS] = {
    3.161094f, 3.896546f, 4.334714f, 89628.726562f, 7403.144531f, 2.017494f, 19.051420f
};

static uint8_t *s_model_buf = nullptr;
static size_t   s_model_len = 0;
static float    s_mean[SEIZURE_MODEL_CHANNELS];
static float    s_std[SEIZURE_MODEL_CHANNELS];
static bool     s_ready = false;
static uint8_t  s_arena[SEIZURE_ARENA_BYTES] __attribute__((aligned(16)));

static tflite::MicroMutableOpResolver<16> s_resolver;
static tflite::MicroInterpreter *s_interp = nullptr;

/* ---------- 内部工具 ---------- */

static bool mount_model_partition(void)
{
    if (esp_spiffs_mounted("model")) {
        return true;
    }
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/model",
        .partition_label = "model",
        .max_files = 3,
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(SEIZURE_TAG, "挂载 model 分区失败: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static bool file_to_buf(const char *path, uint8_t **buf, size_t *len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); return false; }
    uint8_t *data = (uint8_t *)malloc((size_t)sz);
    if (!data) { fclose(fp); return false; }
    if (fread(data, 1, (size_t)sz, fp) != (size_t)sz) {
        free(data);
        fclose(fp);
        return false;
    }
    fclose(fp);
    *buf = data;
    *len = (size_t)sz;
    return true;
}

/* 从 JSON 文本中提取 key 对应的浮点数组（不依赖第三方 JSON 库） */
static bool parse_float_array(const char *buf, const char *key, float *out, int n)
{
    const char *p = strstr(buf, key);
    if (!p) return false;
    p = strchr(p, '[');
    if (!p) return false;
    p++;
    char *end = nullptr;
    for (int i = 0; i < n; i++) {
        while (*p && !(isdigit((unsigned char)*p) || *p == '-' || *p == '+' ||
                       *p == '.' || *p == 'e' || *p == 'E')) {
            p++;
        }
        if (!*p) return false;
        float v = strtof(p, &end);
        if (end == p) return false;
        out[i] = v;
        p = end;
    }
    return true;
}

static void load_normalization(void)
{
    memcpy(s_mean, fallback_mean, sizeof(s_mean));
    memcpy(s_std,  fallback_std,  sizeof(s_std));

    FILE *fp = fopen(SEIZURE_JSON_PATH, "rb");
    if (!fp) {
        ESP_LOGW(SEIZURE_TAG, "normalization.json 不存在，使用回退参数");
        return;
    }
    char buf[512] = {0};
    size_t rd = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    if (rd == 0) return;
    buf[rd] = 0;

    float mean_tmp[SEIZURE_MODEL_CHANNELS] = {0};
    float std_tmp[SEIZURE_MODEL_CHANNELS]  = {0};
    if (parse_float_array(buf, "\"mean\"", mean_tmp, SEIZURE_MODEL_CHANNELS) &&
        parse_float_array(buf, "\"std\"",  std_tmp,  SEIZURE_MODEL_CHANNELS)) {
        memcpy(s_mean, mean_tmp, sizeof(s_mean));
        memcpy(s_std,  std_tmp,  sizeof(s_std));
        ESP_LOGI(SEIZURE_TAG, "归一化参数已从 flash 加载");
    } else {
        ESP_LOGW(SEIZURE_TAG, "normalization.json 解析失败，使用回退参数");
    }
}

/* ---------- 对外接口 ---------- */

void SeizureModel_Init(void)
{
    if (s_ready) return;
    if (!mount_model_partition()) return;

    uint8_t *buf = nullptr;
    size_t len = 0;
    if (!file_to_buf(SEIZURE_MODEL_PATH, &buf, &len)) {
        ESP_LOGE(SEIZURE_TAG, "读取模型失败: %s", SEIZURE_MODEL_PATH);
        return;
    }
    s_model_buf = buf;
    s_model_len = len;
    ESP_LOGI(SEIZURE_TAG, "模型已加载: %u bytes", (unsigned)len);

    load_normalization();

    const tflite::Model *model = tflite::GetModel(s_model_buf);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(SEIZURE_TAG, "模型 schema 版本不匹配");
        free(s_model_buf);
        s_model_buf = nullptr;
        return;
    }

    TfLiteStatus st = s_resolver.AddConv2D();
    if (st == kTfLiteOk) st = s_resolver.AddMaxPool2D();
    if (st == kTfLiteOk) st = s_resolver.AddMean();
    if (st == kTfLiteOk) st = s_resolver.AddFullyConnected();
    if (st == kTfLiteOk) st = s_resolver.AddLogistic();
    if (st == kTfLiteOk) st = s_resolver.AddQuantize();
    if (st == kTfLiteOk) st = s_resolver.AddDequantize();
    if (st == kTfLiteOk) st = s_resolver.AddReshape();
    if (st == kTfLiteOk) st = s_resolver.AddExpandDims();
    if (st != kTfLiteOk) {
        ESP_LOGE(SEIZURE_TAG, "算子注册失败");
        free(s_model_buf);
        s_model_buf = nullptr;
        return;
    }

    s_interp = new (std::nothrow) tflite::MicroInterpreter(
        model, s_resolver, s_arena, SEIZURE_ARENA_BYTES);
    if (!s_interp || s_interp->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(SEIZURE_TAG, "解释器初始化失败");
        delete s_interp;
        s_interp = nullptr;
        free(s_model_buf);
        s_model_buf = nullptr;
        return;
    }
    s_ready = true;
    ESP_LOGI(SEIZURE_TAG, "模型就绪 (输入 %d x %d, arena %u B)",
             s_interp->input(0)->dims->data[1],
             s_interp->input(0)->dims->data[2],
             (unsigned)sizeof(s_arena));
}

bool SeizureModel_Is_Ready(void)
{
    return s_ready;
}

bool SeizureModel_Predict(const float frames[SEIZURE_MODEL_FRAMES][SEIZURE_MODEL_CHANNELS],
                          float *prob_out)
{
    if (!s_ready || !s_interp) return false;
    TfLiteTensor *in = s_interp->input(0);
    if (!in || !in->data.f) return false;
    float *dst = in->data.f;
    for (int f = 0; f < SEIZURE_MODEL_FRAMES; f++) {
        for (int c = 0; c < SEIZURE_MODEL_CHANNELS; c++) {
            dst[f * SEIZURE_MODEL_CHANNELS + c] = (frames[f][c] - s_mean[c]) / s_std[c];
        }
    }
    if (s_interp->Invoke() != kTfLiteOk) return false;
    TfLiteTensor *out = s_interp->output(0);
    float p = out->data.f[0];
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    *prob_out = p;
    return true;
}