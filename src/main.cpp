/**
 * @file main.cpp
 * @brief エントリポイントおよび LVGL/ハードウェア初期化処理.
 *
 * - LVGL 描画バッファとディスプレイドライバの初期化
 * - エンコーダ入力（回転・スイッチ）の割り込み/デバイス登録
 * - I2S 経由のオーディオ出力と BGM/SE タスク起動
 * - メインメニュー用ローラー UI の構築とメニュー遷移制御
 */

#include <Arduino.h>
#include <driver/i2s.h>  // v2.x ではこれ 1 つで OK
#include <lvgl.h>
#include <LovyanGFX.hpp>
#include <Wire.h>

#include "LGFX_ESP32C3-st7735-0.96-80x160-notouch.hpp"
#include "config.h"
#include "include/PCA9539.h"
#include "include/OP.h"
#include "include/sound.h"
#include "freertos/FreeRTOS.h"
#include "game/game.h"
#include "menu_functions.h"

// -----------------------------------------------------------------------------
// グローバル変数
// -----------------------------------------------------------------------------
// SAMPLE_RATE, DMA_BUF_LEN, DMA_BUF_CNT は config.h で定義（I2S 初期化・audio_task で共通）

PCA9539 ioExpander;  // I2C IOエキスパンダ（ボタン・LED制御）
SOUND sound;

// エンコーダ関連
static int32_t encoder_diff = 0;      // ISRで更新されるエンコーダの差分値
static int16_t encoder_acc = 0;       // エンコーダの累積値（±17で1クリックとして処理）
static volatile int32_t encoder_for_stg = 0;  // STG用：生の累積（ゲームが読んでクリア）
lv_indev_t* indev_encoder = nullptr;  // LVGLのエンコーダ入力デバイス（menu_functions.cppから参照される）

// STG用エンコーダ累積の取得・クリア（game.hで宣言）
int32_t get_encoder_stg_delta(void) {
    int32_t v = encoder_for_stg;
    encoder_for_stg = 0;
    return v;
}

#if DEBUG_ENABLED
volatile int32_t raw_encoder_count = 0;  // デバッグ用：エンコーダの生カウント
#endif

// UI要素
static lv_obj_t* roller = nullptr;      // メニュー選択用ローラー
static lv_obj_t* image_obj = nullptr;    // ローラー右側に表示する画像
lv_obj_t* main_screen = nullptr;         // メイン画面（menu_functions.cppから参照される）
lv_group_t* g_main = nullptr;                 // エンコーダ入力用のグループ

// メニュー状態管理
enum MenuState {
    MENU_STATE_ROLLER,    // ローラー画面表示中
    MENU_STATE_EXECUTING  // 実行画面表示中（STAT/GAME/DICE等）
};
static MenuState current_state = MENU_STATE_ROLLER;
static uint16_t last_selected_idx = 0;  // 最後に選択されたメニュー項目のインデックス

//画像データ
extern "C" {
    extern const lv_img_dsc_t BASE_IMAGE;
    extern const lv_img_dsc_t BGIMAGE;
}

// 液晶サイズのデータ
static const uint16_t kScreenWidth = SCREEN_WIDTH;
static const uint16_t kScreenHeight = SCREEN_HEIGHT;
static lv_disp_draw_buf_t display_draw_buffer;
static lv_color_t display_color_buffer[kScreenWidth * kScreenHeight / LVGL_BUFFER_RATIO];
LGFX lcd;


#if LV_USE_LOG != 0
/**
 * @brief LVGL のログをシリアルポートへ出力するコールバック.
 *
 * @param buf LVGL から渡されるログメッセージ文字列
 */
static void my_print(const char* buf) {
    Serial.printf("%s", buf);
    Serial.flush();
}
#endif


/**
 * @brief メニュー操作用のクリック音（短いビープ）を再生する.
 */
static void play_click_sound(void) {
    audio_se_play(BUZZER_FREQ,100);
    audio_se_play(0,10);
#if DEBUG_ENABLED
    Serial.println("Beep!");
#endif
}

/**
 * @brief メニュー決定時の SE（やや派手な効果音）を再生する.
 */
static void select_sound(void){
    audio_se_play(2000,100);
    audio_se_play(0,5);
    audio_se_play(1000,100);
    audio_se_play(0,5);
#if DEBUG_ENABLED
    Serial.println("SELECT!");
#endif
}


// -----------------------------------------------------------------------------
// LVGL コールバック・ユーティリティ
// -----------------------------------------------------------------------------
/**
 * @brief エンコーダ A/B 相の変化を検出して差分カウントを更新する ISR.
 *
 * 4 状態 (00, 01, 10, 11) の遷移テーブルに基づいて 1 ステップ分の
 * 回転方向を `encoder_diff` に蓄積する。
 */
static void IRAM_ATTR read_encoder_isr(void) {
    static uint8_t old_ab = 0;
    static const int8_t enc_states[] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
    old_ab <<= 2;
    old_ab |= (digitalRead(ENCODER_A_PIN) << 1) | digitalRead(ENCODER_B_PIN);
    encoder_diff += enc_states[old_ab & 0x0F];
}




/**
 * @brief LVGL のフラッシュコールバック: 指定領域を TFT に描画する.
 *
 * @param disp_drv LVGL ディスプレイドライバ
 * @param area     更新対象矩形領域
 * @param color_p  ピクセルデータ先頭ポインタ
 */
static void my_disp_flush(lv_disp_drv_t* disp_drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
    lcd.startWrite();
    lcd.setAddrWindow(area->x1, area->y1, w, h);
    lcd.writePixels((lgfx::rgb565_t*)&color_p->full, w * h);
    lcd.endWrite();
    lv_disp_flush_ready(disp_drv);
}

// Main 画面から他画面へ遷移した直後、indev で「押下」を無視するフラグ
static bool indev_suppress_key_until_release = false;

/**
 * @brief エンコーダ「押下」入力の一時抑制フラグを設定する.
 *
 * 画面遷移直後に押しっぱなしの状態を無視するために使用する。
 *
 * @param suppress true で抑制有効, false で無効
 */
void set_indev_suppress_key_until_release(bool suppress) {
    indev_suppress_key_until_release = suppress;
}

/**
 * @brief LVGL のエンコーダ入力コールバック.
 *
 * - 割り込みで更新された `encoder_diff` を累積し、±17 で 1 クリックとみなす
 * - STG 用に生の累積値も保持する
 * - エンコーダスイッチや IO エキスパンダのボタン状態から押下状態を決定する
 *
 * @param drv  LVGL 入力デバイスドライバ
 * @param data 読み取り結果を書き込む構造体
 */
static void encoder_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    
    encoder_acc += encoder_diff;
    encoder_for_stg += encoder_diff;  // STG用に生の累積も保持
    if (encoder_acc >= 17) {
        data->enc_diff = 1;  // 正方向に1クリック
        encoder_acc = 0;
        encoder_diff = 0;
        play_click_sound();
    } else if (encoder_acc <= -17) {
        data->enc_diff = -1;  // 負方向に1クリック
        encoder_acc = 0;
        encoder_diff = 0;
        play_click_sound();
    } else {
        data->enc_diff = 0;  // 閾値未満の場合は変化なし
    }
    // スイッチ状態の読み取り（優先順位: エンコーダスイッチピン > PCA9539 BUTTON1）
    bool real_pressed = false;
    if (ENCODER_SW_PIN >= 0) {
        real_pressed = (digitalRead(ENCODER_SW_PIN) == LOW);
    } else if (ioExpander.isInitialized()) {
        real_pressed = ioExpander.getButton1();
    }
    if (indev_suppress_key_until_release) {
        data->state = LV_INDEV_STATE_REL;  // 遷移直後は常に「離している」と報告
        if (!real_pressed) {
            indev_suppress_key_until_release = false;  // 実際に離したら解除
        }
    } else {
        data->state = real_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    }
}


/**
 * @brief メインメニュー用ローラーのイベントハンドラ.
 *
 * 選択インデックスを `last_selected_idx` に保持し、デバッグ時には
 * 変更や押下をシリアルに出力する。
 *
 * @param e LVGL イベント
 */
static void roller_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* obj = lv_event_get_target(e);
    uint16_t idx = lv_roller_get_selected(obj);
    last_selected_idx = idx;

    if (code == LV_EVENT_VALUE_CHANGED) {
        // ローラーの選択が変わった時
#if DEBUG_ENABLED
        Serial.printf("Roller selected: %u\n", idx);
#endif
    } else if (code == LV_EVENT_PRESSED) {
        // エンコーダのスイッチが押された時
#if DEBUG_ENABLED
        Serial.printf("Item %u pressed\n", idx);
#endif
    }
}

// -----------------------------------------------------------------------------
// メイン画面 UI 構築
// -----------------------------------------------------------------------------

/**
 * @brief メインメニュー用ローラーと画像を生成し、エンコーダ入力に関連付ける.
 *
 * - 親コンテナに背景画像・ローラー・右側画像を配置
 * - `g_main` グループを作成または再利用してローラーを登録
 * - エンコーダ入力デバイス `indev_encoder` に `g_main` を関連付け
 *
 * @param parent メインスクリーンオブジェクト
 */
void setup_roller(lv_obj_t* parent) {
    // エンコーダ入力グループの初期化（既存の場合は古いオブジェクトをクリア）
    if (g_main == nullptr) {
        g_main = lv_group_create();
        lv_indev_set_group(indev_encoder, g_main);
    } else {
        lv_group_remove_all_objs(g_main);
    }

    // 背景画像の表示
    lv_obj_t* image_obj_opbg = lv_img_create(parent);
    
    lv_obj_set_style_bg_img_src(lv_scr_act(), &BGIMAGE, 0);
    //lv_img_set_src(image_obj_opbg, &BGIMAGE);
    lv_obj_set_size(image_obj_opbg, 160, 80);
    lv_obj_align(image_obj_opbg, LV_ALIGN_CENTER, 0, 0);

    // ローラーの作成と設定
    roller = lv_roller_create(parent);
    lv_roller_set_options(roller, ROLLER_OPTIONS, LV_ROLLER_MODE_INFINITE);
    lv_obj_align(roller, LV_ALIGN_LEFT_MID, ROLLER_ALIGN_X, 0);
    lv_obj_set_width(roller, ROLLER_WIDTH);
    lv_obj_set_scrollbar_mode(roller, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_outline_width(roller, 0, LV_STATE_FOCUSED | LV_STATE_EDITED);
    lv_obj_set_style_border_width(roller, 0, LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(roller, LV_OPA_70, 0);   // 70%透明

    // 親（画面/コンテナ）側のスクロールバーを無効化
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    // ローラー側のスクロールバーを無効化（既に入っているならOK）
    lv_obj_set_scrollbar_mode(roller, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(roller, LV_OBJ_FLAG_SCROLLABLE);

    // 画像オブジェクトの作成（ローラーの右側に配置）
    image_obj = lv_img_create(lv_scr_act());
    lv_obj_set_size(image_obj, IMAGE_WIDTH, IMAGE_HEIGHT);
    lv_obj_align(image_obj, LV_ALIGN_LEFT_MID, IMAGE_ALIGN_X, IMAGE_ALIGN_Y);
    lv_img_set_src(image_obj, &BASE_IMAGE);

    // エンコーダ入力グループにローラーを登録し、フォーカスを設定
    if (indev_encoder != nullptr) {
        lv_indev_set_group(indev_encoder, g_main);
    }
    if (roller != nullptr && g_main != nullptr) {
        lv_obj_add_event_cb(roller, roller_event_cb, LV_EVENT_ALL, NULL);
        lv_group_remove_all_objs(g_main);
        lv_group_add_obj(g_main, roller);
        lv_group_focus_obj(roller);
        lv_group_set_editing(g_main, true);  // エディットモードを有効化（ローラーを回転可能にする）
    }
}

/**
 * @brief 実行画面からメインメニューに戻った際にローラー表示とフォーカスを復帰する.
 *
 * - 非表示にしていたローラーと画像を再表示
 * - `g_main` グループにローラーを再登録し、エンコーダ入力とフォーカスを復元
 */
void on_back_button_pressed(void) {
    lv_obj_clear_flag(roller, LV_OBJ_FLAG_HIDDEN);
    if (image_obj != nullptr) {
        lv_obj_clear_flag(image_obj, LV_OBJ_FLAG_HIDDEN);
    }
    // エンコーダグループにローラーを再登録（実行画面でグループが変更されている可能性があるため）
    if (g_main != nullptr && roller != nullptr && indev_encoder != nullptr) {
        lv_group_remove_all_objs(g_main);
        lv_group_add_obj(g_main, roller);
        lv_indev_set_group(indev_encoder, g_main);  // グループをエンコーダに再設定
        lv_group_focus_obj(roller);
        lv_group_set_editing(g_main, true);
    }
}

// -----------------------------------------------------------------------------
// setup / loop
// -----------------------------------------------------------------------------

/**
 * @brief Arduino フレームワークのセットアップ関数.
 *
 * - シリアル・乱数シード・ブザー・未使用ピンの初期化
 * - I2S とオーディオタスクの初期化
 * - エンコーダ割り込みと IO エキスパンダの初期化
 * - LVGL とディスプレイドライバの初期化
 * - ROGO/OP の起動アニメーション実行
 * - NVS からカスタムダイス/統計データを読み込み
 * - メインメニュー用ローラーの構築
 */
void setup(void) {
    Serial.begin(SERIAL_BAUD_RATE);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(UNUSED_PIN_1, OUTPUT);
    digitalWrite(UNUSED_PIN_1, LOW);

    // 乱数シードの初期化（DICE機能用）
    randomSeed(analogRead(A0) + millis());

#if DEBUG_ENABLED
    Serial.printf("LVGL %d.%d.%d\n", lv_version_major(), lv_version_minor(), lv_version_patch());
#endif


    //BGM・SE用の関数
#if SOUND_ENABLED
    // I2S (PDM) 初期化
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_PDM),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = DMA_BUF_CNT,
        .dma_buf_len = DMA_BUF_LEN,
        .use_apll = false
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_PIN_NO_CHANGE,
        .ws_io_num = I2S_PIN_NO_CHANGE,
        .data_out_num = BUZZER_PIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);

    

    audio_init();  /* SEキュー作成（audio_task の前に必須） */
    TaskHandle_t bgm_sound = NULL;

    /* オーディオタスク: 優先度を高くしてDMA切れ（ぶつ切り）を防ぐ */
    xTaskCreate(audio_task, "audio_task", 4096, NULL, 2, &bgm_sound);
    audio_se_play(0,10);
    //vTaskDelete(bgm_sound);
#endif

    // エンコーダピンの設定（A/B相とスイッチ）
    pinMode(ENCODER_A_PIN, INPUT_PULLUP);
    pinMode(ENCODER_B_PIN, INPUT_PULLUP);
    if (ENCODER_SW_PIN >= 0) {
        pinMode(ENCODER_SW_PIN, INPUT_PULLUP);
    }
    attachInterrupt(digitalPinToInterrupt(ENCODER_A_PIN), read_encoder_isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_B_PIN), read_encoder_isr, CHANGE);

    // I2Cバスの初期化（PCA9539用）
    pinMode(I2C_SDA, INPUT_PULLUP);
    pinMode(I2C_SCL, INPUT_PULLUP);
    delay(10);  // バス安定化待ち
    Wire.setPins(I2C_SDA, I2C_SCL);

    // PCA9539 IOエキスパンダの初期化（ボタン・LED制御）
    if (!ioExpander.begin(I2C_SDA, I2C_SCL, I2C_INT_PIN)) {
#if DEBUG_ENABLED
        Serial.println("PCA9539: init failed");
#endif
    } else {
        ioExpander.setLED1(LED1_INIT);
        ioExpander.setLED2(LED2_INIT);
        ioExpander.setLED3(LED3_INIT);
#if DEBUG_ENABLED
        Serial.println("PCA9539: OK");
#endif
    }

    // LVGLの初期化
    lv_init();
#if LV_USE_LOG != 0
    lv_log_register_print_cb(my_print);
#endif

    // ディスプレイの初期化
    lcd.begin();
    lcd.setRotation(DISPLAY_ROTATION);
    lv_disp_draw_buf_init(&display_draw_buffer,
                          display_color_buffer,
                          NULL,
                          (size_t)(kScreenWidth * kScreenHeight / LVGL_BUFFER_RATIO));

    // LVGLディスプレイドライバの登録
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = kScreenWidth;
    disp_drv.ver_res = kScreenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &display_draw_buffer;
    lv_disp_drv_register(&disp_drv);

    // メイン画面の作成とロード
    main_screen = lv_obj_create(NULL);
    lv_scr_load(main_screen);

    //ROGOの呼び出し
    execute_ROGO();

    //起動時のOPの呼び出し
    execute_OP();


    // カスタムダイスとSTATデータを不揮発メモリから読み込み
    load_custom_dice_from_nvs();
    load_stat_from_nvs();

    // LVGLエンコーダ入力デバイスの登録
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_ENCODER;
    indev_drv.read_cb = encoder_read_cb;
    indev_encoder = lv_indev_drv_register(&indev_drv);


    //BGM再生停止
    //vTaskDelete(bgm_sound);

    //メイン画面にローラーと画像を配置
    setup_roller(main_screen);
}

void loop(void) {
    
#if DEBUG_ENABLED
    // エンコーダの生カウントを定期的に出力（デバッグ用）
    static uint32_t last_debug = 0;
    if (millis() - last_debug > DEBUG_ENCODER_INTERVAL) {
        Serial.printf("Encoder: %ld\n", (long)raw_encoder_count);
        last_debug = millis();
    }
#endif

    // ボタン状態の読み取り（エッジ検出用の前回値と更新タイミング管理）
    static bool button1_prev = false;
    static bool button2_prev = false;
    static uint32_t last_button_update = 0;
    const uint32_t BUTTON_UPDATE_INTERVAL_MS = 50;  // ボタン状態更新間隔（I2C負荷軽減）

    bool button1_current = false;
    bool button2_current = false;
    if (ioExpander.isInitialized()) {
        // 一定間隔でボタン状態を更新（INTピンがLOWの時のみI2C通信）
        uint32_t now = millis();
        if (now - last_button_update >= BUTTON_UPDATE_INTERVAL_MS) {
            ioExpander.updateButtonStates();
            last_button_update = now;
        }
        // キャッシュからボタン状態を取得（I2C通信なし）
        button1_current = ioExpander.getButton1();
        button2_current = ioExpander.getButton2();
    }

    // BUTTON1が押された時：ローラー画面でのみメニュー項目を実行
    if (current_state == MENU_STATE_ROLLER && button1_current && !button1_prev) {
        uint16_t selected_idx = lv_roller_get_selected(roller);
        last_selected_idx = selected_idx;
#if DEBUG_ENABLED
        Serial.printf("BUTTON1: menu %u\n", selected_idx);
#endif
        // ローラーと画像を非表示にして実行画面に遷移
        lv_obj_add_flag(roller, LV_OBJ_FLAG_HIDDEN);
        if (image_obj != nullptr) {
            lv_obj_add_flag(image_obj, LV_OBJ_FLAG_HIDDEN);
        }
        current_state = MENU_STATE_EXECUTING;

        // 選択された項目に対応する実行関数を呼び出し
        bool continue_execution = false;
        select_sound();
        switch (selected_idx) {
            case 0: continue_execution = execute_STAT();   break;
            case 1: continue_execution = execute_GAME();    break;
            case 2: continue_execution = execute_DICE();    break;
            case 3: continue_execution = execute_TIMER();   break;
            case 4: continue_execution = execute_SOUND(); break;
            case 5: continue_execution = execute_SETTING(); break;
            default: break;
        }

        // 実行が終了したらローラー画面に戻る
        if (!continue_execution) {
            on_back_button_pressed();
            current_state = MENU_STATE_ROLLER;
#if DEBUG_ENABLED
            Serial.println("Back to roller");
#endif
        }
    }

    // ボタンの前回値を更新（エッジ検出用）
    button1_prev = button1_current;
    button2_prev = button2_current;

    // LVGLのタイマーハンドラを呼び出して画面更新を処理
    lv_timer_handler();
    delay(LVGL_TIMER_DELAY);
}
