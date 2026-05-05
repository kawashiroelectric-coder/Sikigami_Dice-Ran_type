#pragma once

/**
 * @file config.h
 * @brief プロジェクト全体の設定値を定義
 * 
 * ピン番号、通信設定、ディスプレイ設定などの変更はこのファイルで行ってください。
 */

// ============================================================================
// Serial通信設定
// ============================================================================
#define SERIAL_BAUD_RATE 115200

// ============================================================================
// I2C設定（PCA9539用）
// ============================================================================
#define I2C_SDA 4          // I2Cデータ線
#define I2C_SCL 5          // I2Cクロック線
#define I2C_INT_PIN 8      // PCA9539割り込みピン

// ============================================================================
// PCA9539 IOエキスパンダ設定
// ============================================================================
#define PCA9539_I2C_ADDR 0x77  // PCA9539のI2Cアドレス（A0/A1の接続により変更可能）

// PCA9539ポート設定
// ポート0: LED用（出力）
// ポート1: ボタン用（入力）
#define PCA9539_PORT0_LED1 0   // P0_0 (TRUE時LOW)
#define PCA9539_PORT0_LED2 1   // P0_1 (TRUE時LOW)
#define PCA9539_PORT0_LED3 2   // P0_2 (TRUE時LOW)
#define PCA9539_PORT1_BUTTON1 0  // P1_0 (押されたときLOW)
#define PCA9539_PORT1_BUTTON2 1  // P1_1 (押されたときLOW)

// 初期状態（未使用の場合はfalse）
#define BUTTON1_INIT false
#define BUTTON2_INIT false
#define LED1_INIT false
#define LED2_INIT false
#define LED3_INIT false

// ============================================================================
// エンコーダー設定
// ============================================================================
#define ENCODER_A_PIN 20       // エンコーダーA相
#define ENCODER_B_PIN 21       // エンコーダーB相
#define ENCODER_SW_PIN -1      // エンコーダースイッチ（未使用の場合は-1）

// ============================================================================
// ブザー設定
// ============================================================================
#define BUZZER_PIN 0           // ブザー接続ピン
#define BUZZER_FREQ 440       // ブザー周波数（Hz）
#define BUZZER_DURATION 30     // ブザー鳴動時間（ms）
#define SOUND_ENABLED true   // デバッグ出力を有効化
#define SOUND_FREQ 2000
#define SOUND_CH 0
#define SOUND_RESOLUTION 0

// I2S/オーディオタスク用（main の I2S 初期化と sound の audio_task で共通利用）
#define SAMPLE_RATE 16000
#define DMA_BUF_LEN 512   // バッファ大きめでぶつ切り防止
#define DMA_BUF_CNT 4    // バッファ数多めでDMAアンダーラン防止

#define SE_QUEUE_LEN  8

// ============================================================================
// ディスプレイ設定
// ============================================================================
#define SCREEN_WIDTH 160       // ディスプレイ幅（ピクセル）
#define SCREEN_HEIGHT 80      // ディスプレイ高さ（ピクセル）
#define DISPLAY_ROTATION 0    // ディスプレイ回転（0-3）

// LVGLバッファ設定
#define LVGL_BUFFER_RATIO 10  // バッファサイズ = (width * height) / ratio

// ============================================================================
// LVGLローラー設定
// ============================================================================
#define ROLLER_OPTIONS "STAT\nGAME\nDICE\nTIMER\nSOUND\nSETTING"
#define ROLLER_ALIGN_X 10     // ローラーのX位置オフセット
#define ROLLER_WIDTH 80       // ローラーの幅

// ============================================================================
// DICE設定
// ============================================================================
// 固定ダイス + プログラム編集で変更可能なカスタム5種（MAX: 9個・255面）
// ローラー表示は menu_functions で dice_types から "nDnnn" 形式で自動生成

struct DiceType {
    uint8_t count;  // サイコロの個数（1〜9）
    uint8_t faces;  // 面数（1〜255）
};

// カスタムダイス5種：ここを編集して個数・面数を変更（count: 1〜9, faces: 1〜255）
#define DICE_CUSTOM1_COUNT  2
#define DICE_CUSTOM1_FACES  12
#define DICE_CUSTOM2_COUNT  4
#define DICE_CUSTOM2_FACES  6
#define DICE_CUSTOM3_COUNT  1
#define DICE_CUSTOM3_FACES  24
#define DICE_CUSTOM4_COUNT  9
#define DICE_CUSTOM4_FACES  255
#define DICE_CUSTOM5_COUNT  5
#define DICE_CUSTOM5_FACES  255

const DiceType dice_types[] = {
        {1, 2}, {1, 4}, {1, 6}, {1, 8}, {1, 10}, {1, 20}, {1, 100},
        {1, 128}, {2, 3}, {2, 6}, {2, 10}, {3, 6},
        {DICE_CUSTOM1_COUNT, DICE_CUSTOM1_FACES},
        {DICE_CUSTOM2_COUNT, DICE_CUSTOM2_FACES},
        {DICE_CUSTOM3_COUNT, DICE_CUSTOM3_FACES},
        {DICE_CUSTOM4_COUNT, DICE_CUSTOM4_FACES},
        {DICE_CUSTOM5_COUNT, DICE_CUSTOM5_FACES}
    };

const uint8_t NUM_DICE_TYPES = sizeof(dice_types) / sizeof(dice_types[0]);

// ============================================================================
// LVGL画像設定
// ============================================================================
#define IMAGE_WIDTH 60        // 画像の幅（ピクセル）
#define IMAGE_HEIGHT 60       // 画像の高さ（ピクセル）
#define IMAGE_ALIGN_X 95      // 画像のX位置オフセット（ローラーの右側）
#define IMAGE_ALIGN_Y 0       // 画像のY位置オフセット（0=中央揃え）

// ============================================================================
// デバッグ設定
// ============================================================================
#define DEBUG_ENABLED flase     // デバッグ出力を有効化
#define DEBUG_ENCODER_INTERVAL 500  // エンコーダーデバッグ出力間隔（ms）

// ============================================================================
// OTA更新設定（SETTING→OTAで使用）
// ============================================================================
#define OTA_FIRMWARE_URL "https://kawasiroelectric.com/firmware.bin"
#define OTA_PASSWORD_MAX_LEN 64
#define OTA_WIFI_SCAN_MAX 20

// ============================================================================
// その他の設定
// ============================================================================
#define LVGL_TIMER_DELAY 8    // LVGLタイマーハンドラの遅延（ms）

// 未使用ピン（必要に応じて設定）
#define UNUSED_PIN_1 1        // 未使用ピン1（現在LOWに設定）
