#include "include/sound.h"
#include "include/sound_bgm.h"
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <driver/i2s.h>
#include <cmath>

// -----------------------------------------------------------------------------
// SE キュー（連続再生用）
// -----------------------------------------------------------------------------
typedef struct {
    int freq;
    int dur_ms;
} se_item_t;

static QueueHandle_t se_queue = NULL;

// -----------------------------------------------------------------------------
// オーディオタスク用の状態（BGM/SE）
// -----------------------------------------------------------------------------
static volatile bool bgm_playing = false;
static volatile const int16_t* current_bgm_data = nullptr;
static volatile size_t current_bgm_len = 0;
static int bgm_ptr = 0;
/* STAT用: BGM再生時間をBEACH/BONFIRE/RAIN別に集計 */
static uint32_t stat_bgm_play_ms[3] = {0, 0, 0};
static uint32_t bgm_play_start_ms = 0;
static uint8_t bgm_play_index = 0;
static volatile bool play_se = false;
static volatile int se_freq = 0;
static volatile int se_sample_count = 0;
static volatile int se_duration_samples = 0;
uint32_t se_phase_acc = 0;        // 位相アキュムレータ

// 全体ボリューム（1〜3）: BGM/SE共通
static uint8_t g_audio_volume = 2;

// PCM SE 用（HIT_SE など）
extern const int16_t HIT_SE[];
extern const size_t HIT_SE_LEN;
static volatile const int16_t* se_pcm_data = nullptr;
static volatile size_t se_pcm_len = 0;
static volatile size_t se_pcm_pos = 0;
static volatile bool se_use_pcm = false;

// -----------------------------------------------------------------------------
// BGMデータ（BEACH, BONFIRE, RAIN - sound_bgm.h + BEACH_BGM.c / BONFIRE_BGM.c / RAIN_BGM.c）
// int16_t PCM 16kHz、audio_taskでループ再生
// -----------------------------------------------------------------------------

/**
 * @brief BGMデータ取得（0=BEACH, 1=BONFIRE, 2=RAIN）
 */
void get_bgm_data(int index, const int16_t** out_data, size_t* out_len) {
    if (out_data) *out_data = nullptr;
    if (out_len) *out_len = 0;
    if (index == 0) {
        if (out_data) *out_data = BEACH_BGM;
        if (out_len) *out_len = BEACH_BGM_LEN;
    } else if (index == 1) {
        if (out_data) *out_data = BONFIRE_BGM;
        if (out_len) *out_len = BONFIRE_BGM_LEN;
    } else if (index == 2) {
        if (out_data) *out_data = RAIN_BGM;
        if (out_len) *out_len = RAIN_BGM_LEN;
    }
}

// -----------------------------------------------------------------------------
// FreeRTOS オーディオタスク（I2S へ BGM/SE をミックスして出力）
// -----------------------------------------------------------------------------
void audio_task(void* param) {
    int16_t mix_buf[DMA_BUF_LEN];
    size_t bytes_written;

    while (1) {
        for (int i = 0; i < DMA_BUF_LEN; i++) {
            int32_t sample = 0;
            if (bgm_playing && current_bgm_data != nullptr && current_bgm_len > 0) {
                sample = (int32_t)current_bgm_data[bgm_ptr++] * (int32_t)g_audio_volume;
                if (bgm_ptr >= (int)current_bgm_len) bgm_ptr = 0;
            }
            int32_t sample_se = 0;
            if (play_se) {
                if (se_use_pcm && se_pcm_data != nullptr && se_pcm_len > 0) {
                    sample_se = (int32_t)se_pcm_data[se_pcm_pos++] * (int32_t)g_audio_volume;
                    if (se_pcm_pos >= se_pcm_len) {
                        play_se = false;
                        se_use_pcm = false;
                        se_pcm_data = nullptr;
                        se_pcm_len = 0;
                        se_pcm_pos = 0;
                    }
                } else {
                    se_phase_acc += se_freq;
                    sample_se = ((se_phase_acc & 0x80000000) ? 20000 : -20000) * (int32_t)g_audio_volume;
                    se_sample_count++;
                    if (se_sample_count >= se_duration_samples) {
                        play_se = false;
                    }
                }
                sample += sample_se;
            }
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;
            mix_buf[i] = (int16_t)sample;
        }
        /* 現在のSEが終わったらキューから次のSEを開始 */
        if (!play_se && se_queue != NULL) {
            se_item_t next;
            if (xQueueReceive(se_queue, &next, 0) == pdTRUE) {
                se_freq = (uint32_t)(((uint64_t)next.freq << 32) / 16000);
                se_duration_samples = (16000 * next.dur_ms) / 1000;
                se_sample_count = 0;
                se_phase_acc = 0;
                play_se = true;
            }
        }
        i2s_write(I2S_NUM_0, mix_buf, sizeof(mix_buf), &bytes_written, portMAX_DELAY);
    }
}

void audio_bgm_play(int index) {
    const int16_t* data = nullptr;
    size_t len = 0;
    get_bgm_data(index, &data, &len);
    if (data != nullptr && len > 0) {
        current_bgm_data = data;
        current_bgm_len = len;
        bgm_ptr = 0;
        bgm_play_index = (index <= 2) ? (uint8_t)index : 0;
        bgm_play_start_ms = (uint32_t)millis();
        bgm_playing = true;
    }
}

void audio_bgm_stop(void) {
    if (bgm_playing && bgm_play_index < 3) {
        uint32_t elapsed = (uint32_t)millis() - bgm_play_start_ms;
        stat_bgm_play_ms[bgm_play_index] += elapsed;
    }
    bgm_playing = false;
    current_bgm_data = nullptr;
    current_bgm_len = 0;
    bgm_ptr = 0;
}

void get_sound_play_time_ms(uint32_t* out_beach_ms, uint32_t* out_bonfire_ms, uint32_t* out_rain_ms) {
    if (out_beach_ms)  *out_beach_ms  = stat_bgm_play_ms[0];
    if (out_bonfire_ms) *out_bonfire_ms = stat_bgm_play_ms[1];
    if (out_rain_ms)   *out_rain_ms   = stat_bgm_play_ms[2];
}

void set_sound_play_time_ms(uint32_t beach_ms, uint32_t bonfire_ms, uint32_t rain_ms) {
    stat_bgm_play_ms[0] = beach_ms;
    stat_bgm_play_ms[1] = bonfire_ms;
    stat_bgm_play_ms[2] = rain_ms;
}

bool audio_bgm_toggle(int index) {
    if (bgm_playing) {
        audio_bgm_stop();
        return false;
    } else {
        audio_bgm_play(index);
        return true;
    }
}

bool audio_bgm_is_playing(void) {
    return bgm_playing;
}

void audio_set_volume(uint8_t vol) {
    if (vol < 1) vol = 1;
    if (vol > 3) vol = 3;
    g_audio_volume = vol;
}

uint8_t audio_get_volume(void) {
    return g_audio_volume;
}

void audio_init(void) {
    if (se_queue == NULL) {
        se_queue = xQueueCreate(SE_QUEUE_LEN, sizeof(se_item_t));
    }
}

void audio_se_play(int freq, int dur) {
    // 通常の矩形波SE（キュー経由）
    if (se_queue != NULL) {
        se_item_t e = { freq, dur };
        if (xQueueSend(se_queue, &e, 0) != pdTRUE) {
            /* キュー満杯: 新しいSEは捨てる */
        }
        return;
    }
    /* キュー未初期化時は従来どおり即時再生 */
    se_use_pcm = false;
    se_pcm_data = nullptr;
    se_pcm_len = 0;
    se_pcm_pos = 0;
    se_freq = (uint32_t)(((uint64_t)freq << 32) / 16000);
    se_duration_samples = (16000 * dur) / 1000;
    se_sample_count = 0;
    se_phase_acc = 0;
    play_se = true;
}

void audio_se_play_pcm(const int16_t* data, size_t len) {
    if (!data || len == 0) return;
    // PCM SE 再生（任意の int16_t 配列）
    se_use_pcm = true;
    se_pcm_data = data;
    se_pcm_len = len;
    se_pcm_pos = 0;
    se_freq = 0;
    se_sample_count = 0;
    se_duration_samples = 0;
    se_phase_acc = 0;
    play_se = true;
}

void audio_se_play_hit(void) {
    // 互換用: HIT_SE を鳴らすラッパ
    audio_se_play_pcm(HIT_SE, HIT_SE_LEN);
}

// -----------------------------------------------------------------------------

/**
 * @brief コンストラクタ
 */
SOUND::SOUND(uint8_t PIN) 
    :_error(false), _initialized(false)
     {
}


/**
 * @brief 初期化
 */
bool SOUND::begin(int PIN, int CH, int RES,int FREQ) {
    _error = false;
    ledcAttachPin(BUZZER_PIN,CH);
    ledcSetup(CH,FREQ,RES);
    _initialized = true;
    
#if DEBUG_ENABLED
    Serial.println("BUZZER: Initialized successfully");
#endif

    return true;
}



/**
 * @brief デバイスの存在確認
 */
//bool SOUND::BGM(uint16_t* BGM_FREQ, uint16_t* BGM_DUR) {
//}


    