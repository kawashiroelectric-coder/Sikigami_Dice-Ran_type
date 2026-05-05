#pragma once

#include <Arduino.h>
#include <stddef.h>
#include "config.h"

// -----------------------------------------------------------------------------
// BGMデータ取得（sound.cppで実装）
// index: 0=BEACH, 1=BONFIRE, 2=RAIN
// -----------------------------------------------------------------------------
void get_bgm_data(int index, const int16_t** out_data, size_t* out_len);

// -----------------------------------------------------------------------------
// オーディオ初期化（SEキュー作成）。main で xTaskCreate(audio_task) の前に呼ぶこと
// -----------------------------------------------------------------------------
void audio_init(void);

// -----------------------------------------------------------------------------
// FreeRTOS オーディオタスク（sound.cppで実装、main で xTaskCreate に渡す）
// -----------------------------------------------------------------------------
void audio_task(void* param);

// -----------------------------------------------------------------------------
// BGM 再生制御（sound.cppで実装）
// -----------------------------------------------------------------------------
void audio_bgm_play(int index);     /* 0=BEACH, 1=BONFIRE, 2=RAIN */
void audio_bgm_stop(void);          /* 強制停止（Main戻る時に呼ぶ） */
bool audio_bgm_toggle(int index);   /* 再生中なら停止・停止中なら再生。戻り値: トグル後の再生中ならtrue、停止中ならfalse */
bool audio_bgm_is_playing(void);    /* 現在BGM再生中ならtrue */
void audio_set_volume(uint8_t vol); /* 全体ボリューム(1〜3)を設定 */
uint8_t audio_get_volume(void);     /* 現在の全体ボリュームを取得 */
void audio_se_play(int freq, int dur_ms);                 /* 周波数(Hz), 再生時間(ms) - 矩形波SE */
void audio_se_play_pcm(const int16_t* data, size_t len);  /* 任意PCM SEを再生（data: int16_t, len: サンプル数） */
void audio_se_play_hit(void);                             /* HIT_SE のPCM SEを再生（ラッパ） */

/* STAT用: BEACH/BONFIRE/RAIN の総再生時間(ms)を取得 */
void get_sound_play_time_ms(uint32_t* out_beach_ms, uint32_t* out_bonfire_ms, uint32_t* out_rain_ms);
/* STAT用: NVSから復元する際に再生時間を設定 */
void set_sound_play_time_ms(uint32_t beach_ms, uint32_t bonfire_ms, uint32_t rain_ms);

// -----------------------------------------------------------------------------

/**
 * @file sound.h
 * @brief sound 16ビットI/Oエキスパンダのドライバークラス
 * 
 * soundはGPIO0にPWMで音声を流すための物です。
 */
class SOUND {
    public:
    /**
     * @brief コンストラクタ
     * @param BUZZER_PIN config.h内で定義されたピン番号）
     */
    SOUND(uint8_t PIN = BUZZER_PIN);

    /**
     * @brief 初期化
     * @param sda I2C SDAピン番号（デフォルト: config.hのI2C_SDA）
     * @param scl I2C SCLピン番号（デフォルト: config.hのI2C_SCL）
     * @param int_pin 割り込みピン番号（デフォルト: config.hのI2C_INT_PIN、未使用の場合は-1）
     * @return 初期化成功時true、失敗時false
     */
    bool begin(int PIN = BUZZER_PIN, int CH = SOUND_CH, int RES = SOUND_RESOLUTION, int FREQ = SOUND_FREQ);
    /**
     * @brief BGMの再生
     * @param BGM_FREQ　周波数
     * @param BGM_DUR 秒数
     * @return ポート1の8ビット値
     */
    bool BGM(uint16_t* BGM_FREQ, uint16_t* BGM_DUR);
    /**
     * @brief ポート1全体の状態を読み取り
     * @return ポート1の8ビット値
     */
    bool SE();



    private:

    bool _error;             // エラーフラグ
    bool _initialized;       // 初期化済みフラグ


};