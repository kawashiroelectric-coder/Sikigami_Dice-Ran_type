/**
 * @file menu_functions.cpp
 * @brief メインメニュー配下の各機能画面（STAT / DICE / TIMER / SOUND / SETTING）の実装.
 *
 * 主な責務:
 * - NVS を用いた統計情報・カスタムダイス・タイマー設定・サウンド統計・音量設定の永続化
 * - STAT 画面での GAME/DICE/TIMER/SOUND の統計表示
 * - DICE 画面でのダイス選択・ロール結果表示・統計更新
 * - TIMER 画面でのポモドーロタイマー実行と集中/休憩時間の編集
 * - SOUND 画面での環境音 BGM の選択と再生
 * - SETTING 画面での DICE/TIMER/VOLUME/OTA/CREDIT の各サブ画面遷移
 */

#include <Arduino.h>
#include <lvgl.h>
#include <Preferences.h>

#include "config.h"
#include "include/PCA9539.h"
#include "include/sound.h"
#include "menu_functions.h"

// Dice 設定・NVS キー
#define NUM_FIXED_DICE   12
#define NUM_CUSTOM_DICE  5
#define NVS_NAMESPACE    "dice"
#define NVS_KEY_CUSTOM   "custom"

// 統計情報用 NVS キー
#define NVS_NAMESPACE_STAT "stat"
#define NVS_KEY_SNAKE_HI   "snake_hi"
#define NVS_KEY_STG_HI     "stg_hi"
#define NVS_KEY_DICE_CNT   "dice_cnt"
#define NVS_KEY_DICE_SUM   "dice_sum"
#define NVS_KEY_DICE_SQ    "dice_sq"
#define NVS_KEY_POMODORO   "pomodoro"
#define NVS_KEY_TIMER_WORK  "timer_work"
#define NVS_KEY_TIMER_BREAK "timer_break"
#define NVS_KEY_SND_BCH    "snd_beach"
#define NVS_KEY_SND_BON    "snd_bonfire"
#define NVS_KEY_SND_RAIN   "snd_rain"
#define NVS_KEY_VOLUME     "volume"

// PCA9539の外部参照（main.cppで定義）
extern PCA9539 ioExpander;
// メイン画面（実行画面から戻る際に再ロードする）
extern lv_obj_t* main_screen;
// エンコーダ入力グループ（main.cppで定義）
extern lv_group_t* g_main;
extern lv_indev_t* indev_encoder;

//画像データ

extern "C" {
    extern const lv_img_dsc_t STAT;
    extern const lv_img_dsc_t DICE;
    extern const lv_img_dsc_t TIMER;
    extern const lv_img_dsc_t BREAK;
    extern const lv_img_dsc_t BGIMAGE;
    extern const lv_img_dsc_t LOADING;
    extern const lv_img_dsc_t DICEBG;
    extern const lv_img_dsc_t BEACH;
    extern const lv_img_dsc_t BONFIRE;
    extern const lv_img_dsc_t RAIN;
}


/* 背景画像のリスト（あらかじめ用意した画像配列など） */
static const void * SOUND[] = {
    &BEACH, // 変換済みの画像データ
    &BONFIRE,
    &RAIN
};
static int8_t current_bg_idx = 0;
static lv_obj_t * bg_img_obj; // 背景画像オブジェクト

// STAT画面: 選択データ（GAME/DICE/TIMER/SOUND）
static const char* const STAT_DATA_OPTIONS[] = {"GAME", "DICE", "TIMER", "SOUND"};
static const int STAT_DATA_OPTIONS_N = 4;
static int current_stat_idx = 0;
static lv_obj_t* stat_label_title = nullptr;
static lv_obj_t* stat_label_content = nullptr;  /* 統計数値表示用 */

int32_t sound_time = 0;
int32_t pomodoro_count = 0;
int32_t pomodoro_timer = 0;
/* TIMERの集中・休憩分数（SETTINGで変更、NVSに保存。デフォルト25分・5分） */
static uint8_t pomodoro_work_min = 25;
static uint8_t pomodoro_break_min = 5;

// DICE統計: タイプごとに回数・合計・二乗和（分散用）
static uint32_t g_dice_roll_count[NUM_DICE_TYPES];
static uint32_t g_dice_roll_sum[NUM_DICE_TYPES];
static uint64_t g_dice_roll_sum_sq[NUM_DICE_TYPES];

/* GAME統計: ハイスコア（SNAKE=len, STG=score） */
static uint32_t stat_snake_high = 0;
static uint32_t stat_stg_high = 0;

// カスタムダイス5種（不揮発メモリから読み書き、config.h の初期値は初回用）
static uint8_t g_custom_dice_count[NUM_CUSTOM_DICE];
static uint8_t g_custom_dice_faces[NUM_CUSTOM_DICE];

/**
 * @brief ダイス種別インデックスから個数と面数を取得する.
 *
 * @param idx        0〜11: 固定ダイス, 12〜16: カスタムダイス
 * @param out_count  個数 (1〜9)
 * @param out_faces  面数 (1〜255)
 */
static void get_dice_for_index(int idx, uint8_t* out_count, uint8_t* out_faces) {
    if (idx < NUM_FIXED_DICE) {
        *out_count = dice_types[idx].count;
        *out_faces = dice_types[idx].faces;
        return;
    }
    int custom_index = idx - NUM_FIXED_DICE;
    if (custom_index >= 0 && custom_index < NUM_CUSTOM_DICE) {
        uint8_t count = g_custom_dice_count[custom_index];
        uint8_t faces = g_custom_dice_faces[custom_index];
        *out_count = (count >= 1 && count <= 9) ? count : 1;
        *out_faces = (faces >= 1 && faces <= 255) ? faces : 6;
        return;
    }
    *out_count = 1;
    *out_faces = 6;
}

/**
 * @brief NVS からカスタムダイス設定を読み込む.
 *
 * - 保存済みデータがあればそれを使用
 * - 初回など保存がない場合は config.h の `dice_types` から初期値を読み込む
 */
void load_custom_dice_from_nvs(void) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return;
    uint8_t nvs_buffer[10];
    if (prefs.getBytes(NVS_KEY_CUSTOM, nvs_buffer, 10) == 10) {
        for (int i = 0; i < NUM_CUSTOM_DICE; i++) {
            uint8_t count = nvs_buffer[i * 2];
            uint8_t faces = nvs_buffer[i * 2 + 1];
            g_custom_dice_count[i] = (count >= 1 && count <= 9) ? count : 1;
            g_custom_dice_faces[i] = (faces >= 1 && faces <= 255) ? faces : 6;
        }
    } else {
        for (int i = 0; i < NUM_CUSTOM_DICE; i++) {
            g_custom_dice_count[i] = dice_types[NUM_FIXED_DICE + i].count;
            g_custom_dice_faces[i] = dice_types[NUM_FIXED_DICE + i].faces;
        }
    }
    prefs.end();
}

/**
 * @brief 現在のカスタムダイス設定を NVS に保存する.
 */
static void save_custom_dice_to_nvs(void) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return;
    uint8_t nvs_buffer[10];
    for (int i = 0; i < NUM_CUSTOM_DICE; i++) {
        uint8_t count = g_custom_dice_count[i];
        uint8_t faces = g_custom_dice_faces[i];
        nvs_buffer[i * 2]     = (count >= 1 && count <= 9) ? count : 1;
        nvs_buffer[i * 2 + 1] = (faces >= 1 && faces <= 255) ? faces : 6;
    }
    prefs.putBytes(NVS_KEY_CUSTOM, nvs_buffer, 10);
    prefs.end();
}

/**
 * @brief STAT 用の全データを NVS から読み込む.
 *
 * - SNAKE/STG ハイスコア
 * - DICE 統計（回数・合計・二乗和）
 * - ポモドーロ回数および集中/休憩時間
 * - SOUND 再生時間および音量
 */
void load_stat_from_nvs(void) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE_STAT, true)) return;
    stat_snake_high = prefs.getUInt(NVS_KEY_SNAKE_HI, 0);
    stat_stg_high = prefs.getUInt(NVS_KEY_STG_HI, 0);
    size_t dice_cnt_len = prefs.getBytesLength(NVS_KEY_DICE_CNT);
    if (dice_cnt_len == NUM_DICE_TYPES * sizeof(uint32_t)) {
        prefs.getBytes(NVS_KEY_DICE_CNT, g_dice_roll_count, dice_cnt_len);
    }
    size_t dice_sum_len = prefs.getBytesLength(NVS_KEY_DICE_SUM);
    if (dice_sum_len == NUM_DICE_TYPES * sizeof(uint32_t)) {
        prefs.getBytes(NVS_KEY_DICE_SUM, g_dice_roll_sum, dice_sum_len);
    }
    size_t dice_sq_len = prefs.getBytesLength(NVS_KEY_DICE_SQ);
    if (dice_sq_len == NUM_DICE_TYPES * sizeof(uint64_t)) {
        prefs.getBytes(NVS_KEY_DICE_SQ, g_dice_roll_sum_sq, dice_sq_len);
    }
    pomodoro_count = prefs.getLong(NVS_KEY_POMODORO, 0);
    pomodoro_work_min = (uint8_t)prefs.getUChar(NVS_KEY_TIMER_WORK, 25);
    if (pomodoro_work_min < 1 || pomodoro_work_min > 60) pomodoro_work_min = 25;
    pomodoro_break_min = (uint8_t)prefs.getUChar(NVS_KEY_TIMER_BREAK, 5);
    if (pomodoro_break_min < 1 || pomodoro_break_min > 30) pomodoro_break_min = 5;
    uint32_t bm = prefs.getULong(NVS_KEY_SND_BCH, 0);
    uint32_t bom = prefs.getULong(NVS_KEY_SND_BON, 0);
    uint32_t rm = prefs.getULong(NVS_KEY_SND_RAIN, 0);
    uint8_t vol = prefs.getUChar(NVS_KEY_VOLUME, 2);
    if (vol < 1 || vol > 3) vol = 2;
    prefs.end();
    set_sound_play_time_ms(bm, bom, rm);
    audio_set_volume(vol);
}

/**
 * @brief STAT 用の全データを NVS へ保存する.
 *
 * 上記 `load_stat_from_nvs()` が読み込む内容と同一の情報を永続化する。
 */
static void save_stat_to_nvs(void) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE_STAT, false)) return;
    prefs.putUInt(NVS_KEY_SNAKE_HI, stat_snake_high);
    prefs.putUInt(NVS_KEY_STG_HI, stat_stg_high);
    prefs.putBytes(NVS_KEY_DICE_CNT, g_dice_roll_count, NUM_DICE_TYPES * sizeof(uint32_t));
    prefs.putBytes(NVS_KEY_DICE_SUM, g_dice_roll_sum, NUM_DICE_TYPES * sizeof(uint32_t));
    prefs.putBytes(NVS_KEY_DICE_SQ, g_dice_roll_sum_sq, NUM_DICE_TYPES * sizeof(uint64_t));
    prefs.putLong(NVS_KEY_POMODORO, pomodoro_count);
    prefs.putUChar(NVS_KEY_TIMER_WORK, pomodoro_work_min);
    prefs.putUChar(NVS_KEY_TIMER_BREAK, pomodoro_break_min);
    uint32_t bm, bom, rm;
    get_sound_play_time_ms(&bm, &bom, &rm);
    prefs.putULong(NVS_KEY_SND_BCH, bm);
    prefs.putULong(NVS_KEY_SND_BON, bom);
    prefs.putULong(NVS_KEY_SND_RAIN, rm);
    prefs.putUChar(NVS_KEY_VOLUME, audio_get_volume());
    prefs.end();
}

/**
 * @brief GAME モジュールからハイスコアを報告する（STAT 用に永続化される）.
 *
 * SNAKE の長さと STG のスコアのいずれかが既存のハイスコアを上回った場合に
 * NVS へ統計情報を保存する。
 *
 * @param snake_len 現在プレイで達成した SNAKE の長さ
 * @param stg_score 現在プレイで達成した STG のスコア
 */
void game_report_high_score(int snake_len, uint32_t stg_score) {
    bool changed = false;
    if (snake_len > 0 && (uint32_t)snake_len > stat_snake_high) {
        stat_snake_high = (uint32_t)snake_len;
        changed = true;
    }
    if (stg_score > 0 && stg_score > stat_stg_high) {
        stat_stg_high = stg_score;
        changed = true;
    }
    if (changed) save_stat_to_nvs();
}

/**
 * @brief メニュー操作用の「決定」SE を再生する.
 */
static void select_sound(void) {
    audio_se_play(2000,100);
    audio_se_play(0,5);
    audio_se_play(1000,100);
    audio_se_play(0,5);
}

//動作関係のクラス
// エンコーダ入力を処理するコールバック
//SOUND用
static void bg_change_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    uint32_t key = lv_event_get_key(e);

    if(code == LV_EVENT_KEY) {
        if(key == LV_KEY_RIGHT || key == LV_KEY_NEXT) {
            current_bg_idx++;
        } else if(key == LV_KEY_LEFT || key == LV_KEY_PREV) {
            current_bg_idx--;
        }

        /* 範囲内に収める（ループさせる場合） */
        if(current_bg_idx >= 3) current_bg_idx = 0;
        if(current_bg_idx < 0) current_bg_idx = 2;

        /* 画像ソースを更新 */
        lv_img_set_src(bg_img_obj, SOUND[current_bg_idx]);
    }
}

/* 統計表示内容を現在の current_stat_idx に合わせて更新 */
static void update_stat_content(void) {
    if (stat_label_content == nullptr) return;
    static char buf[96];
    buf[0] = '\0';

    switch (current_stat_idx) {
        case 0: /* GAME: SNAKE/STGハイスコア */
            snprintf(buf, sizeof(buf), "SNAKE:%lu\nSTG:%lu",
                (unsigned long)stat_snake_high, (unsigned long)stat_stg_high);
            break;
        case 1: { /* DICE: 回数・平均・分散のばらつき */
            uint32_t total_rolls = 0;
            for (int i = 0; i < NUM_DICE_TYPES; i++) total_rolls += g_dice_roll_count[i];
            if (total_rolls == 0) {
                snprintf(buf, sizeof(buf), "Rolls:0");
            } else {
                int len = snprintf(buf, sizeof(buf), "Rolls:%lu\n", (unsigned long)total_rolls);
                for (int i = 0; i < NUM_DICE_TYPES && len < (int)sizeof(buf) - 20; i++) {
                    if (g_dice_roll_count[i] == 0) continue;
                    uint8_t dc, df;
                    get_dice_for_index(i, &dc, &df);
                    uint32_t n = g_dice_roll_count[i];
                    double avg = (double)g_dice_roll_sum[i] / (double)n;
                    double var = (double)g_dice_roll_sum_sq[i] / (double)n - avg * avg;
                    if (var < 0.0) var = 0.0;
                    len += snprintf(buf + len, sizeof(buf) - (size_t)len, "%uD%u:%lu %.1f %.1f\n",
                        (unsigned)dc, (unsigned)df, (unsigned long)n, avg, var);
                }
            }
            break;
        }
        case 2: /* TIMER: 総終了回数 */
            snprintf(buf, sizeof(buf), "Done:%ld", (long)pomodoro_count);
            break;
        case 3: { /* SOUND: BEACH/BONFIRE/RAIN 総再生時間(分) */
            uint32_t beach_ms = 0, bonfire_ms = 0, rain_ms = 0;
            get_sound_play_time_ms(&beach_ms, &bonfire_ms, &rain_ms);
            uint32_t bm = beach_ms / 60000, bom = bonfire_ms / 60000, rm = rain_ms / 60000;
            snprintf(buf, sizeof(buf), "Beach:%lum \nBonfire:%lum \nRain:%lum", (unsigned long)bm, (unsigned long)bom, (unsigned long)rm);
            break;
        }
        default:
            snprintf(buf, sizeof(buf), "---");
            break;
    }
    lv_label_set_text(stat_label_content, buf);
}

/* 統計表示エリア: エンコーダ上下でスクロール */
static void stat_content_scroll_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    uint32_t key = lv_event_get_key(e);
    if (code != LV_EVENT_KEY) return;
    lv_obj_t * target = lv_event_get_target(e);
    const int scroll_step = 12;
    if (key == LV_KEY_UP || key == LV_KEY_LEFT) {
        lv_obj_scroll_by(target, 0, scroll_step, LV_ANIM_ON);
    } else if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT) {
        lv_obj_scroll_by(target, 0, -scroll_step, LV_ANIM_ON);
    }
}

// エンコーダ入力を処理するコールバック（STAT画面: rectラベルを GAME/DICE/TIMER/SOUND で切り替え）
static void stat_text_change_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    uint32_t key = lv_event_get_key(e);

    if(code == LV_EVENT_KEY) {
        if(key == LV_KEY_RIGHT || key == LV_KEY_NEXT) {
            current_stat_idx++;
        } else if(key == LV_KEY_LEFT || key == LV_KEY_PREV) {
            current_stat_idx--;
        }

        /* 範囲内に収める（ループ） */
        if(current_stat_idx >= STAT_DATA_OPTIONS_N) current_stat_idx = 0;
        if(current_stat_idx < 0) current_stat_idx = STAT_DATA_OPTIONS_N - 1;

        /* rect内ラベルのテキストを更新 */
        if(stat_label_title != nullptr) {
            lv_label_set_text(stat_label_title, STAT_DATA_OPTIONS[current_stat_idx]);
        }
        update_stat_content();
    }
}








/**
 * @brief STAT メニュー項目を実行し、統計情報画面を表示する.
 *
 * 表示内容:
 * - GAME: SNAKE/STG のハイスコア
 * - DICE: 各ダイスタイプの回数・平均・分散
 * - TIMER: ポモドーロ回数・集中/休憩時間
 * - SOUND: BGM 再生時間
 *
 * @retval true  連続実行を継続したい場合（現在は常に false）
 * @retval false メインメニューへ戻る
 */
bool execute_STAT() {
    // ==========================================
    // ここにSTAT項目の実行内容を実装してください
    //全項目での統計的なデータ表示
    //DICE　出目の分布
    //TIMER 使用時間・合計終了回数
    //SOUND　再生時間・回数
    //項目ごとに何回使用しているか等
    // ==========================================
   // --- 画面生成 ---
    lv_obj_t * stat_screen = lv_obj_create(NULL);

    lv_scr_load(stat_screen);

    //背景画像の生成
    lv_obj_t* image_obj_stat = lv_img_create(stat_screen);
    lv_obj_set_style_bg_img_src(lv_scr_act(), &STAT, 0);

    //中央上部の表示
    lv_obj_t * rect = lv_obj_create(lv_scr_act());  // 今の画面に追加
    lv_obj_set_size(rect, 120, 20);                  // 幅40px 高さ20px
    lv_obj_set_style_radius(rect, 4, 0);            //角を丸める(4px)
    lv_obj_set_style_border_width(rect, 1, 0);      //1pxの枠線を付ける
    lv_obj_set_style_border_color(rect, lv_color_black(), 0);   //枠線を黒色にする
    lv_obj_align(rect, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_set_style_bg_color(rect, lv_color_make(30, 30, 50), 0);
    lv_obj_set_style_bg_opa(rect, LV_OPA_70, 0);

    lv_obj_t * label_title = lv_label_create(rect);
    current_stat_idx = 0;
    stat_label_title = label_title;
    lv_label_set_text(label_title, STAT_DATA_OPTIONS[current_stat_idx]);
    lv_obj_align(label_title, LV_ALIGN_CENTER, 0, 0);

    /* 統計数値表示エリア（rectの下）。Button1でフォーカス後エンコーダで上下スクロール */
    lv_obj_t * rect_content = lv_obj_create(lv_scr_act());
    lv_obj_set_size(rect_content, 140, 42);
    lv_obj_set_style_radius(rect_content, 4, 0);
    lv_obj_set_style_border_width(rect_content, 1, 0);
    lv_obj_set_style_border_color(rect_content, lv_color_black(), 0);
    lv_obj_align(rect_content, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(rect_content, lv_color_make(25, 25, 40), 0);
    lv_obj_set_style_bg_opa(rect_content, LV_OPA_90, 0);
    lv_obj_set_scrollbar_mode(rect_content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(rect_content, LV_DIR_VER);  /* 縦方向のみスクロール（左右ずれ防止） */

    lv_obj_t * label_content = lv_label_create(rect_content);
    stat_label_content = label_content;
    lv_obj_set_width(label_content, 132);
    lv_label_set_long_mode(label_content, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label_content, &lv_font_montserrat_12, 0);
    lv_obj_align(label_content, LV_ALIGN_TOP_LEFT, 2, 1);
    update_stat_content();

    //Back ボタンの設定
    lv_obj_t * rect_u1 = lv_obj_create(lv_scr_act());  // 今の画面に追加
    lv_obj_set_size(rect_u1, 60, 30);                  // 幅40px 高さ20px
    lv_obj_set_style_radius(rect_u1, 4, 0);            //角を丸める(4px)
    lv_obj_set_style_border_width(rect_u1, 1, 0);      //1pxの枠線を付ける
    lv_obj_set_style_border_color(rect_u1, lv_color_black(), 0);   //枠線を黒色にする
    lv_obj_align(rect_u1, LV_ALIGN_BOTTOM_LEFT, -5, 10);
    
    lv_obj_set_style_bg_color(rect_u1, lv_color_make(30, 30, 50), 0);
    lv_obj_set_style_bg_opa(rect_u1, LV_OPA_70, 0);

    lv_obj_t * label1 = lv_label_create(rect_u1);
    lv_label_set_text(label1, "Back");
    lv_obj_align(label1, LV_ALIGN_TOP_RIGHT, 5, -10);

    
    /* エンコーダ入力: 項目選択時はrectのみ、統計スクロール時はrect_contentのみをグループに登録。
     * 両方入れるとエンコーダのナビゲートでフォーカスが飛び、4項目すべて選べなくなる */
    if (g_main != nullptr && indev_encoder != nullptr) {
        lv_group_remove_all_objs(g_main);
        lv_obj_add_flag(rect, LV_OBJ_FLAG_CLICKABLE);
        lv_group_add_obj(g_main, rect);
        lv_obj_add_event_cb(rect, stat_text_change_event_cb, LV_EVENT_KEY, NULL);
        lv_obj_add_flag(rect_content, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(rect_content, stat_content_scroll_cb, LV_EVENT_KEY, NULL);
        lv_indev_set_group(indev_encoder, g_main);
        lv_group_focus_obj(rect);
        lv_group_set_editing(g_main, true);
    }
    
    // 親（画面/コンテナ）側のスクロールバーを無効化
    lv_obj_set_scrollbar_mode(stat_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(stat_screen, LV_OBJ_FLAG_SCROLLABLE);

    // 画面中央上部表示のスクロールバーを無効化
    lv_obj_set_scrollbar_mode(rect, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(rect, LV_OBJ_FLAG_SCROLLABLE);

    // Backボタンのスクロールバーを無効化（既に入っているならOK）
    lv_obj_set_scrollbar_mode(rect_u1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(rect_u1, LV_OBJ_FLAG_SCROLLABLE);
    // Startボタンのスクロールバーを無効化（既に入っているならOK）
    //lv_obj_set_scrollbar_mode(rect_u2, LV_SCROLLBAR_MODE_OFF);
    //lv_obj_clear_flag(rect_u2, LV_OBJ_FLAG_SCROLLABLE);
     
    
    // BUTTON2が押されるまでループ
    static bool button1_prev = false;
    static bool button2_prev = false;
    static uint32_t last_button_update = 0;
    const uint32_t BUTTON_UPDATE_INTERVAL = 50; // ボタン状態更新間隔（ms）
    bool button_free = false;

    while (true) {
        // LVGLタイマーハンドラを呼び出す（画面更新のため）
        lv_timer_handler();
        
        // BUTTON2が押されたら終了（ローラーに戻る）
        bool button1_current = false;
        bool button2_current = false;
        
        if (ioExpander.isInitialized()) {
            // 一定間隔でボタン状態を更新（INTピンがLOWの時のみI2C通信）
            uint32_t now = millis();
            if (now - last_button_update >= BUTTON_UPDATE_INTERVAL) {
                ioExpander.updateButtonStates();
                last_button_update = now;
            }
            
            // キャッシュからボタン状態を取得（I2C通信なし）
            button1_current = ioExpander.getButton1();
            button2_current = ioExpander.getButton2();
        }
        
        // Rollerからの選択時に押されていた連続検知防止
        if (!button1_current && !button2_current && !button_free) {
            button_free = true;
        }
        
        if (button1_current && !button1_prev && button_free) {
            /* Button1: 項目選択→統計エリアへ。グループをrect_contentのみに切り替え */
            if (g_main != nullptr && lv_group_get_focused(g_main) == rect) {
                lv_group_remove_all_objs(g_main);
                lv_group_add_obj(g_main, rect_content);
                lv_group_focus_obj(rect_content);
                lv_group_set_editing(g_main, true);
                select_sound();
            }
        }
        if (button2_current && !button2_prev) {
            if (g_main != nullptr && lv_group_get_focused(g_main) == rect_content) {
                /* Button2: 統計エリア→項目選択へ。グループをrectのみに切り替え */
                lv_obj_scroll_to(rect_content, 0, 0, LV_ANIM_OFF);
                if (indev_encoder != nullptr) {
                    lv_indev_reset(indev_encoder, rect);
                }
                lv_group_remove_all_objs(g_main);
                lv_group_add_obj(g_main, rect);
                lv_indev_set_group(indev_encoder, g_main);
                lv_group_focus_obj(rect);
                lv_group_set_editing(g_main, true);
                select_sound();
            } else {
                /* Button2: 項目選択にフォーカス中ならメインに戻る */
                save_stat_to_nvs();
                select_sound();
                stat_label_title = nullptr;
                stat_label_content = nullptr;
                lv_scr_load(main_screen);
                lv_obj_del(stat_screen);
                return false; // ローラーに戻る
            }
        }
        button1_prev = button1_current;
        button2_prev = button2_current;
        
        delay(LVGL_TIMER_DELAY); // CPU負荷軽減
    }
    
    return false;
}

/**
 * @brief DICE メニュー項目を実行し、ダイス選択とロール結果画面を表示する.
 *
 * - LIST 画面でダイスタイプ（固定 + カスタム）をローラーから選択
 * - Button1 でサイコロを振り、結果と合計値を結果画面に表示
 * - 統計用に各タイプの回数・合計・二乗和を更新し NVS に保存
 *
 * @retval true  連続実行を継続したい場合（現在は常に false）
 * @retval false メインメニューへ戻る
 */
bool execute_DICE() {
    // LIST画面の作成
    lv_obj_t* list_screen = lv_obj_create(NULL);

    lv_scr_load(list_screen);
    set_indev_suppress_key_until_release(true);

    //背景画像の生成
    lv_obj_t* image_obj_opbg = lv_img_create(list_screen);
    lv_obj_set_style_bg_img_src(lv_scr_act(), &DICEBG, 0);

    // ローラー用オプションを dice_types から生成（全項目を nDn 形式で表示、カスタムは先頭に*を付ける）
    static char dice_roller_options[256];
    int opt_len = 0;
    for (int i = 0; i < NUM_DICE_TYPES && opt_len < (int)sizeof(dice_roller_options) - 8; i++) {
        uint8_t c, f;
        get_dice_for_index(i, &c, &f);
        const char * prefix = (i >= NUM_FIXED_DICE) ? "*" : " ";
        opt_len += snprintf(dice_roller_options + opt_len, sizeof(dice_roller_options) - (size_t)opt_len,
            "%s%s%uD%u", (i > 0) ? "\n" : "", prefix, (unsigned)c, (unsigned)f);
    }

    //ローラーの作成
    lv_obj_t* list_roller = lv_roller_create(list_screen);
    lv_roller_set_options(list_roller, dice_roller_options, LV_ROLLER_MODE_INFINITE);
    lv_obj_align(list_roller, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_width(list_roller, 80);
    lv_obj_set_scrollbar_mode(list_roller, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_outline_width(list_roller, 0, LV_STATE_FOCUSED | LV_STATE_EDITED);
    lv_obj_set_style_border_width(list_roller, 0, LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(list_roller, LV_OPA_70, 0);   // 70%透明


    // 画像オブジェクトの作成（ローラーの右側に配置）
    lv_obj_t* image_obj_list = lv_img_create(list_screen);
    lv_obj_set_size(image_obj_list, IMAGE_WIDTH, IMAGE_HEIGHT);
    lv_obj_align(image_obj_list, LV_ALIGN_LEFT_MID, IMAGE_ALIGN_X, IMAGE_ALIGN_Y);
    lv_img_set_src(image_obj_list, &DICE);
    //lv_obj_t* label_hint = lv_label_create(list_screen);
    //lv_label_set_text(label_hint, "BTN1:Roll  BTN2:Back");
    //lv_obj_align(label_hint, LV_ALIGN_BOTTOM_MID, 0, -5);

    // 親（画面/コンテナ）側のスクロールバーを無効化
    lv_obj_set_scrollbar_mode(list_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(list_screen, LV_OBJ_FLAG_SCROLLABLE);

    // ローラー側のスクロールバーを無効化（既に入っているならOK）
    lv_obj_set_scrollbar_mode(list_roller, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(list_roller, LV_OBJ_FLAG_SCROLLABLE);
    
    // エンコーダ入力グループの設定（画面ロード前に設定）
    if (g_main != nullptr && indev_encoder != nullptr) {
        lv_group_remove_all_objs(g_main);
        lv_group_add_obj(g_main, list_roller);
        lv_indev_set_group(indev_encoder, g_main);  // グループをエンコーダに再設定
        lv_group_focus_obj(list_roller);
        lv_group_set_editing(g_main, true);
    }
    

    
    // 状態管理
    enum DiceState {
        DICE_STATE_LIST,      // LIST画面表示中
        DICE_STATE_RESULT     // 結果表示中
    };
    DiceState state = DICE_STATE_LIST;
    
    lv_obj_t* result_screen = nullptr;
    lv_obj_t* label_result = nullptr;
    lv_obj_t* label_result_under = nullptr;
    lv_obj_t* list_roller_ptr = list_roller;  // スコープ外でも参照できるように保持
    bool button1_prev = false;
    bool button2_prev = false;
    bool button_free = false;  // ROLLERからの選択時に押されていた連続検知防止
    uint32_t last_button_update = 0;
    const uint32_t BUTTON_UPDATE_INTERVAL = 50;

    while (true) {
        lv_timer_handler();

        bool button1_current = false;
        bool button2_current = false;
        
        if (ioExpander.isInitialized()) {
            uint32_t now = millis();
            if (now - last_button_update >= BUTTON_UPDATE_INTERVAL) {
                ioExpander.updateButtonStates();
                last_button_update = now;
            }
            button1_current = ioExpander.getButton1();
            button2_current = ioExpander.getButton2();
        }

        // ROLLERからの選択時に押されていた連続検知防止
        if (!button1_current && !button2_current && !button_free) {
            button_free = true;
        }

        if (state == DICE_STATE_LIST) {
            // LIST画面での処理
            if (button1_current && !button1_prev && button_free) {
                // BUTTON1: サイコロを振る
                select_sound();
                uint16_t selected_idx = lv_roller_get_selected(list_roller_ptr);
                if (selected_idx < NUM_DICE_TYPES) {
                    uint8_t dice_count, dice_faces;
                    get_dice_for_index((int)selected_idx, &dice_count, &dice_faces);
                    
                    // サイコロを振る（乱数シードはsetup()で初期化済み）最大9個・1〜faces面
                    uint16_t total = 0;
                    uint8_t dice_results[9] = {0};
                    uint8_t n = (dice_count <= 9 && dice_count >= 1) ? dice_count : 1;
                    int faces_max = (dice_faces >= 1 && dice_faces <= 255) ? (int)dice_faces : 6;
                    for (uint8_t i = 0; i < n; i++) {
                        dice_results[i] = (uint8_t)random(1, faces_max + 1);
                        total += dice_results[i];
                    }
                    
                    // 結果画面の作成
                    result_screen = lv_obj_create(NULL);
                    
                    //背景画像の生成
                    lv_obj_t* image_obj_result = lv_img_create(result_screen);
                    lv_img_set_src(image_obj_result, &DICEBG);
                    lv_obj_set_size(image_obj_result, 160, 80);
                    lv_obj_align(image_obj_result, LV_ALIGN_CENTER, 0, 0);

                    
                    //中央上部の表示
                    lv_obj_t * rect = lv_obj_create(result_screen);  // 今の画面に追加
                    lv_obj_set_size(rect, 120, 20);                  // 幅40px 高さ20px
                    lv_obj_set_style_radius(rect, 4, 0);            //角を丸める(4px)
                    lv_obj_set_style_border_width(rect, 1, 0);      //1pxの枠線を付ける
                    lv_obj_set_style_border_color(rect, lv_color_black(), 0);   //枠線を黒色にする
                    lv_obj_align(rect, LV_ALIGN_TOP_MID, 0, 0);
                    
                    // サイコロタイプを表示
                    lv_obj_t* label_type = lv_label_create(rect);
                    char type_text[16];
                    snprintf(type_text, sizeof(type_text), "%s%uD%u",
                             (selected_idx >= NUM_FIXED_DICE) ? "*" : " ",
                             (unsigned)dice_count, (unsigned)dice_faces);
                    lv_label_set_text(label_type, type_text);
                    lv_obj_align(label_type, LV_ALIGN_CENTER, 0, 0);


                    // 結果表示エリア（rectの下 
                    lv_obj_t * rect_content = lv_obj_create(result_screen);
                    lv_obj_set_size(rect_content, 155, 42);
                    lv_obj_set_style_radius(rect_content, 4, 0);
                    lv_obj_set_style_border_width(rect_content, 1, 0);
                    lv_obj_set_style_border_color(rect_content, lv_color_black(), 0);
                    lv_obj_align(rect_content, LV_ALIGN_CENTER, 0, 0);
                    lv_obj_set_style_bg_color(rect_content, lv_color_make(25, 25, 40), 0);
                    lv_obj_set_style_bg_opa(rect_content, LV_OPA_90, 0);
                    
                    // 結果を表示（出目のみ "a, b, c, d"。5個以上は2行に改行）
                    label_result = lv_label_create(rect_content);
                    label_result_under = lv_label_create(rect_content);
                    char result_text[80];
                    char result_text_under[80];
                    int len = 0;
                    int len_under = 0;
                    if (n >= 5) {
                        uint8_t first_line = (uint8_t)((n + 1) / 2);  // 1行目の個数
                        for (uint8_t i = 0; i < first_line && len < (int)sizeof(result_text) - 4; i++) {
                            len += snprintf(result_text + len, sizeof(result_text) - (size_t)len,
                                "%s%u", (i > 0) ? "  " : "", (unsigned)dice_results[i]);
                        }
                        //len += snprintf(result_text + len, sizeof(result_text) - (size_t)len, "\n");
                        for (uint8_t i = first_line; i < n && len < (int)sizeof(result_text_under) - 4; i++) {
                            len_under += snprintf(result_text_under + len_under, sizeof(result_text_under) - (size_t)len_under,
                                "%s%u", (i > first_line) ? "  " : "", (unsigned)dice_results[i]);
                        }
                    } else {
                        for (uint8_t i = 0; i < n && len < (int)sizeof(result_text) - 4; i++) {
                            len += snprintf(result_text + len, sizeof(result_text) - (size_t)len,
                                "%s%u", (i > 0) ? "  " : "", (unsigned)dice_results[i]);
                        }
                    }
                    if (n >= 5) {
                        lv_label_set_text(label_result, result_text);
                        lv_obj_align(label_result, LV_ALIGN_TOP_MID, 0, -10);
                        lv_label_set_text(label_result_under, result_text_under);
                        lv_obj_align(label_result_under, LV_ALIGN_BOTTOM_MID, 0, 10);
                        lv_obj_clear_flag(label_result_under, LV_OBJ_FLAG_HIDDEN);
                    } else {
                        lv_label_set_text(label_result, result_text);
                        lv_obj_align(label_result, LV_ALIGN_CENTER, 0, 0);
                        lv_label_set_text(label_result_under, "");  /* 2行目は未使用なので空にして非表示 */
                        lv_obj_add_flag(label_result_under, LV_OBJ_FLAG_HIDDEN);
                    }

                    /* STAT用: 出目を記録（回数・合計・二乗和） */
                    if (selected_idx < NUM_DICE_TYPES) {
                        g_dice_roll_count[selected_idx]++;
                        g_dice_roll_sum[selected_idx] += total;
                        g_dice_roll_sum_sq[selected_idx] += (uint64_t)total * (uint64_t)total;
                        save_stat_to_nvs();
                    }
                    
                    //Back ボタンの設定
                    lv_obj_t * rect_u1 = lv_obj_create(result_screen);  // 今の画面に追加
                    lv_obj_set_size(rect_u1, 60, 30);                  // 幅40px 高さ20px
                    lv_obj_set_style_radius(rect_u1, 4, 0);            //角を丸める(4px)
                    lv_obj_set_style_border_width(rect_u1, 1, 0);      //1pxの枠線を付ける
                    lv_obj_set_style_border_color(rect_u1, lv_color_black(), 0);   //枠線を黒色にする
                    lv_obj_align(rect_u1, LV_ALIGN_BOTTOM_LEFT, -5, 10);
    
                    lv_obj_set_style_bg_color(rect_u1, lv_color_make(30, 30, 50), 0);
                    lv_obj_set_style_bg_opa(rect_u1, LV_OPA_70, 0);

                    lv_obj_t * label1 = lv_label_create(rect_u1);
                    lv_label_set_text(label1, "Back");
                    lv_obj_align(label1, LV_ALIGN_TOP_RIGHT, 5, -10);

                    
                    // rectのスクロールバーを無効化
                    lv_obj_set_scrollbar_mode(rect_u1, LV_SCROLLBAR_MODE_OFF);
                    lv_obj_clear_flag(rect_u1, LV_OBJ_FLAG_SCROLLABLE);
                    // sscreenのスクロールバーを無効化
                    lv_obj_set_scrollbar_mode(result_screen, LV_SCROLLBAR_MODE_OFF);
                    lv_obj_clear_flag(result_screen, LV_OBJ_FLAG_SCROLLABLE);


                    // 画面中央上部表示のスクロールバーを無効化
                    lv_obj_set_scrollbar_mode(rect, LV_SCROLLBAR_MODE_OFF);
                    lv_obj_clear_flag(rect, LV_OBJ_FLAG_SCROLLABLE);

                    // Backボタンのスクロールバーを無効化（既に入っているならOK）
                    lv_obj_set_scrollbar_mode(rect_content, LV_SCROLLBAR_MODE_OFF);
                    lv_obj_clear_flag(rect_content, LV_OBJ_FLAG_SCROLLABLE);
                    // ヒント表示
                    //lv_obj_t* label_hint_result = lv_label_create(result_screen);
                    //lv_label_set_text(label_hint_result, "BTN2:Back");
                    //lv_obj_align(label_hint_result, LV_ALIGN_BOTTOM_MID, 0, -5);
                    
                    lv_scr_load(result_screen);
                    state = DICE_STATE_RESULT;
                }
            } else if (button2_current && !button2_prev) {
                // BUTTON2: ROLLERに戻る（先にmain_screenをロードしてから削除）
                select_sound();
                lv_scr_load(main_screen);
                lv_obj_del(list_screen);
                return false;
            }
        } else if (state == DICE_STATE_RESULT) {
            // 結果画面での処理
            if (button2_current && !button2_prev) {
                // BUTTON2: LISTに戻る（先にlist_screenをロードしてから削除）
                select_sound();
                lv_scr_load(list_screen);
                lv_obj_del(result_screen);
                result_screen = nullptr;
                label_result = nullptr;
                state = DICE_STATE_LIST;
                
                // エンコーダグループを再設定（LIST画面に戻る）
                if (g_main != nullptr && indev_encoder != nullptr && list_roller_ptr != nullptr) {
                    lv_group_remove_all_objs(g_main);
                    lv_group_add_obj(g_main, list_roller_ptr);
                    lv_indev_set_group(indev_encoder, g_main);  // グループをエンコーダに再設定
                    lv_group_focus_obj(list_roller_ptr);
                    lv_group_set_editing(g_main, true);
                }
            }
        }

        button1_prev = button1_current;
        button2_prev = button2_current;
        delay(LVGL_TIMER_DELAY);
    }
    
    return false;
}

/**
 * @brief TIMER メニュー項目を実行し、ポモドーロタイマー画面を表示する.
 *
 * - 集中時間/休憩時間は `pomodoro_work_min` / `pomodoro_break_min` に従う
 * - Button1: タイマー開始／一時停止／再開／次フェーズへの遷移
 * - Button2: いつでもメインメニュー（ローラー）へ戻る
 *
 * @retval true  連続実行を継続したい場合（現在は常に false）
 * @retval false メインメニューへ戻る
 */
bool execute_TIMER() {
    // ポモドーロタイマー（25分）を実装
    // Button1: タイマースタート（待機中／終了後のみ有効）
    // Button2: どの状態でも即座にローラーメニューへ戻る

    // --- 画面生成 ---
    lv_obj_t * timer_screen = lv_obj_create(NULL);

    lv_scr_load(timer_screen);
    set_indev_suppress_key_until_release(true);

    
    //背景画像の生成
    lv_obj_t* image_obj_opbg = lv_img_create(timer_screen);
    lv_img_set_src(image_obj_opbg, &BGIMAGE);
    lv_obj_set_size(image_obj_opbg, 160, 80);
    lv_obj_align(image_obj_opbg, LV_ALIGN_CENTER, 0, 0);

    //文字強調用の半透明の黒い背景生成
    lv_obj_t* rect_overlay = lv_obj_create(timer_screen);
    lv_obj_set_size(rect_overlay, 140, 55);
    lv_obj_align(rect_overlay, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_color(rect_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(rect_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(rect_overlay, 0, 0);
    lv_obj_set_style_radius(rect_overlay, 4, 0);

    lv_obj_t * label_title = lv_label_create(timer_screen);
    lv_label_set_text(label_title, "Pomodoro Timer");
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 5);

    lv_obj_t * label_time = lv_label_create(timer_screen);
    char time_buf[24];  /* 表示用（"25:00" や "Work (25 min)" 等） */
    snprintf(time_buf, sizeof(time_buf), "%02u:00", (unsigned)pomodoro_work_min);
    lv_label_set_text(label_time, time_buf);
    lv_obj_align(label_time, LV_ALIGN_CENTER, 0, -6);

    // 進捗バー（経過時間の可視化）
    lv_obj_t * bar = lv_bar_create(timer_screen);
    lv_obj_set_size(bar, 85, 12);
    lv_obj_align_to(bar,rect_overlay, LV_ALIGN_BOTTOM_MID, 0, 10);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_make(50, 50, 50), LV_PART_MAIN);
    /* 25分＝青、5分休憩＝緑（休憩開始時に切り替え） */
    lv_obj_set_style_bg_color(bar, lv_color_make(80, 140, 200), LV_PART_INDICATOR);
    
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);  /* 開始後は進捗バーを非表示 */

    //backボタンの構造体
    lv_obj_t * rect_u1 = lv_obj_create(timer_screen);  // 今の画面に追加
    lv_obj_set_size(rect_u1, 60, 30);                  // 幅40px 高さ20px
    lv_obj_set_style_radius(rect_u1, 4, 0);            //角を丸める(4px)
    lv_obj_set_style_border_width(rect_u1, 1, 0);      //1pxの枠線を付ける
    lv_obj_set_style_border_color(rect_u1, lv_color_black(), 0);   //枠線を黒色にする
    lv_obj_align(rect_u1, LV_ALIGN_BOTTOM_LEFT, -5, 10);
    
    lv_obj_set_style_bg_color(rect_u1, lv_color_make(30, 30, 50), 0);
    lv_obj_set_style_bg_opa(rect_u1, LV_OPA_70, 0);

    lv_obj_t * label1 = lv_label_create(rect_u1);
    lv_label_set_text(label1, "Back");
    lv_obj_align(label1, LV_ALIGN_TOP_RIGHT, 5, -10);
    
    //rectのスクロールバーを無効化
    lv_obj_set_scrollbar_mode(rect_u1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(rect_u1, LV_OBJ_FLAG_SCROLLABLE);
    
    //startボタンの構造体
    lv_obj_t * rect_u2 = lv_obj_create(timer_screen);  // 今の画面に追加
    lv_obj_set_size(rect_u2, 60, 30);                  // 幅40px 高さ20px
    lv_obj_set_style_radius(rect_u2, 4, 0);            //角を丸める(4px)
    lv_obj_set_style_border_width(rect_u2, 1, 0);      //1pxの枠線を付ける
    lv_obj_set_style_border_color(rect_u2, lv_color_black(), 0);   //枠線を黒色にする
    lv_obj_align(rect_u2, LV_ALIGN_BOTTOM_RIGHT, 5, 10);

    lv_obj_set_style_bg_color(rect_u2, lv_color_make(30, 30, 50), 0);
    lv_obj_set_style_bg_opa(rect_u2, LV_OPA_70, 0);

    lv_obj_t * label2 = lv_label_create(rect_u2);
    lv_label_set_text(label2, "Start");
    lv_obj_align(label2, LV_ALIGN_TOP_LEFT, -5, -10);
    
    // rectのスクロールバーを無効化
    lv_obj_set_scrollbar_mode(rect_u2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(rect_u2, LV_OBJ_FLAG_SCROLLABLE);

    // 画像オブジェクトの作成（ローラーの右側に配置）
    lv_obj_t* image_obj_list = lv_img_create(timer_screen);
    lv_obj_set_size(image_obj_list, IMAGE_WIDTH, IMAGE_HEIGHT);
    lv_obj_align(image_obj_list, LV_ALIGN_LEFT_MID, IMAGE_ALIGN_X, IMAGE_ALIGN_Y);
    lv_img_set_src(image_obj_list, &TIMER);
    lv_img_set_zoom(image_obj_list, 200);


    // 親（画面/コンテナ）側のスクロールバーを無効化
    lv_obj_set_scrollbar_mode(timer_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(timer_screen, LV_OBJ_FLAG_SCROLLABLE);


    // --- タイマー状態管理 ---
    enum TimerState {
        TIMER_IDLE,
        TIMER_RUNNING,
        TIMER_BREAK,    /* 25分終了後の5分休憩 */
        TIMER_FINISHED,
        TIMER_STOP,
        TIMER_STOP_BREAK
    };

    TimerState state = TIMER_IDLE;

    // 集中・休憩時間（分）は SETTING で変更可能（NVS保存）
    uint32_t POMODORO_DURATION_MS = (uint32_t)pomodoro_work_min * 60UL * 1000UL;
    uint32_t POMODORO_BREAK_MS   = (uint32_t)pomodoro_break_min * 60UL * 1000UL;
    uint32_t start_ms = 0;
    uint32_t start_break_ms = 0;  /* 休憩開始時刻 */
    uint32_t stop_ms = 0;
    uint32_t stop_ms_total = 0;
    uint32_t stop_time = 0;
    uint32_t break_stop_elapsed = 0;  /* 休憩一時停止時の経過時間（ms） */
    bool finished_notified = false;
    bool break_finished_notified = false;

    // --- ボタン状態管理 ---
    bool button1_prev = false;
    bool button2_prev = false;
    uint32_t last_button_update = 0;
    const uint32_t BUTTON_UPDATE_INTERVAL = 50;
    bool button_free = false;

    while (true) {
        // 画面更新
        lv_timer_handler();

        bool button1_current = false;
        bool button2_current = false;
        

        if (ioExpander.isInitialized()) {
            uint32_t now = millis();
            if (now - last_button_update >= BUTTON_UPDATE_INTERVAL) {
                ioExpander.updateButtonStates();
                last_button_update = now;
            }

            // キャッシュからボタン状態を取得
            button1_current = ioExpander.getButton1();
            button2_current = ioExpander.getButton2();
        }

        // Rollerからの選択時に押されていた連続検知防止
        if (!button1_current && !button2_current && !button_free) {
            button_free = true;
        }

        // --- Button2: どの状態でもローラーに戻る ---
        if (button2_current && !button2_prev) {
            save_stat_to_nvs();
            // 先にメイン画面を再ロードしてからタイマー画面を削除する。
            // アクティブ画面を del してから戻ると LVGL が壊れてリセットするため。
            select_sound();
            lv_scr_load(main_screen);
            lv_obj_del(timer_screen);
            return false; // ローラーに戻る
        }

        // --- Button1: タイマースタート（待機中 or 終了後） ---
        if (button1_current && !button1_prev && button_free) {
            if (state == TIMER_IDLE || state == TIMER_FINISHED) {
                state = TIMER_RUNNING;
                select_sound();
                start_ms = millis();

                //rectのサイズ調整
                lv_obj_set_size(rect_overlay, 110, 55);
                lv_obj_align(rect_overlay, LV_ALIGN_CENTER, -20, -10);
                lv_obj_set_style_bg_color(rect_overlay, lv_color_black(), 0);
                lv_obj_set_style_border_width(rect_overlay, 0, 0);
                lv_obj_set_style_radius(rect_overlay, 4, 0);

                
                lv_obj_align_to(bar,rect_overlay, LV_ALIGN_BOTTOM_MID, 0, 10);

                finished_notified = false;
                break_finished_notified = false;
                lv_bar_set_value(bar, 0, LV_ANIM_OFF);
                lv_obj_set_style_bg_color(bar, lv_color_make(80, 140, 200), LV_PART_INDICATOR);  /* 集中＝青 */
                snprintf(time_buf, sizeof(time_buf), "Focus (%u min)", (unsigned)pomodoro_work_min);
                lv_label_set_text(label_title, time_buf);
                lv_obj_align_to(label_title,rect_overlay, LV_ALIGN_TOP_MID, 0, -10);
                lv_label_set_text(label2, "Stop");
                lv_obj_align_to(label_time,rect_overlay, LV_ALIGN_CENTER, 3, 5);
                //lv_obj_add_flag(image_obj_list, LV_OBJ_FLAG_HIDDEN);  /* 開始後は画像を非表示 */
                lv_obj_align(image_obj_list, LV_ALIGN_RIGHT_MID, 0, -5);
                lv_img_set_src(image_obj_list, &LOADING);
                lv_img_set_zoom(image_obj_list, 250);
                lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);  /* 進捗バーを再表示 */
            }else if (state == TIMER_STOP) {
                select_sound();
                stop_ms_total += millis() - stop_ms;
                state = TIMER_RUNNING;
                snprintf(time_buf, sizeof(time_buf), "Focus (%u min)", (unsigned)pomodoro_work_min);
                lv_label_set_text(label_title, time_buf);
                lv_label_set_text(label2, "Stop");
            }else if(state == TIMER_RUNNING){
                select_sound();
                state = TIMER_STOP;
                lv_label_set_text(label_title, "Focus Stop");
                lv_label_set_text(label2, "Start");
                stop_ms = millis();
            }else if (state == TIMER_BREAK) {
                select_sound();
                break_stop_elapsed = millis() - start_break_ms;
                state = TIMER_STOP_BREAK;
                lv_label_set_text(label_title, "Break Stop");
                lv_label_set_text(label2, "Start");
            }else if (state == TIMER_STOP_BREAK) {
                select_sound();
                start_break_ms = millis() - break_stop_elapsed;
                state = TIMER_BREAK;
                snprintf(time_buf, sizeof(time_buf), "Break (%u min)", (unsigned)pomodoro_break_min);
                lv_label_set_text(label_title, time_buf);
                lv_label_set_text(label2, "Stop");
            }
        }

        button1_prev = button1_current;
        button2_prev = button2_current;

        // --- タイマー動作 ---
        if (state == TIMER_RUNNING) {
            uint32_t now = millis();
            uint32_t elapsed = now - start_ms - stop_ms_total;

            if (elapsed >= POMODORO_DURATION_MS) {
                // 集中終了 → 休憩へ
                state = TIMER_BREAK;
                stop_ms = 0;
                stop_ms_total = 0;
                start_break_ms = millis();
                break_finished_notified = false;
                lv_bar_set_value(bar, 0, LV_ANIM_OFF);
                lv_obj_set_style_bg_color(bar, lv_color_make(80, 200, 110), LV_PART_INDICATOR);  /* 休憩＝緑 */
                snprintf(time_buf, sizeof(time_buf), "Break (%u min)", (unsigned)pomodoro_break_min);
                lv_label_set_text(label_title, time_buf);
                lv_label_set_text(label2, "Stop");
                snprintf(time_buf, sizeof(time_buf), "%02u:00", (unsigned)pomodoro_break_min);
                lv_label_set_text(label_time, time_buf);
                lv_img_set_src(image_obj_list, &BREAK);
                pomodoro_count++;
                save_stat_to_nvs();
                if (!finished_notified) {
                    finished_notified = true;
                    audio_se_play(1000, 100);
                }
            } else {
                stop_time = elapsed;
                uint32_t remaining = POMODORO_DURATION_MS - elapsed;
                uint32_t total_sec = remaining / 1000UL;
                uint16_t minutes = total_sec / 60UL;
                uint8_t seconds = total_sec % 60UL;

                char buf[6];
                snprintf(buf, sizeof(buf), "%02u:%02u", minutes, seconds);
                lv_label_set_text(label_time, buf);
                /* 進捗バー: 25分の経過割合 0..100（青のまま） */
                lv_bar_set_value(bar, (int32_t)((elapsed * 100UL) / POMODORO_DURATION_MS), LV_ANIM_OFF);
            }
        } else if (state == TIMER_BREAK) {
            uint32_t now = millis();
            uint32_t break_elapsed = now - start_break_ms;

            if (break_elapsed >= POMODORO_BREAK_MS) {
                state = TIMER_FINISHED;
                lv_bar_set_value(bar, 100, LV_ANIM_OFF);
                lv_obj_clear_flag(image_obj_list, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(label_time, "00:00");
                lv_label_set_text(label_title, "Finished");
                lv_obj_align_to(label_title,rect_overlay, LV_ALIGN_TOP_MID, 0, -10);
                lv_label_set_text(label2, "Re?");
                if (!break_finished_notified) {
                    break_finished_notified = true;
                    audio_se_play(800, 150);
                }
            } else {
                uint32_t remaining = POMODORO_BREAK_MS - break_elapsed;
                uint32_t total_sec = remaining / 1000UL;
                uint16_t minutes = total_sec / 60UL;
                uint8_t seconds = total_sec % 60UL;
                char buf[6];
                snprintf(buf, sizeof(buf), "%02u:%02u", minutes, seconds);
                lv_label_set_text(label_time, buf);
                lv_bar_set_value(bar, (int32_t)((break_elapsed * 100UL) / POMODORO_BREAK_MS), LV_ANIM_OFF);
            }
        } else if (state == TIMER_IDLE) {
            // 待機状態では設定された分数を表示、進捗は0
            snprintf(time_buf, sizeof(time_buf), "%02u:00", (unsigned)pomodoro_work_min);
            lv_label_set_text(label_time, time_buf);
            lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        } else if (state == TIMER_FINISHED) {
            // 終了後はButton1で再スタート可能、進捗100%のまま
        } else if(state == TIMER_STOP){
                // 待機状態では固定表示（25:00）のまま、進捗は0
                uint32_t remaining = POMODORO_DURATION_MS - stop_time;
                uint32_t total_sec = remaining / 1000UL;
                uint16_t minutes = total_sec / 60UL;
                uint8_t seconds = total_sec % 60UL;
                char buf[6];
                snprintf(buf, sizeof(buf), "%02u:%02u", minutes, seconds);
                lv_label_set_text(label_time, buf);
                /* 進捗バー: 経過割合 0..100 */
                lv_bar_set_value(bar, (int32_t)((stop_time * 100UL) / POMODORO_DURATION_MS), LV_ANIM_OFF);
        } else if(state == TIMER_STOP_BREAK){
                // 休憩一時停止: 残り時間と進捗バーを休憩用に表示
                uint32_t remaining = POMODORO_BREAK_MS - break_stop_elapsed;
                uint32_t total_sec = remaining / 1000UL;
                uint16_t minutes = total_sec / 60UL;
                uint8_t seconds = total_sec % 60UL;
                char buf[6];
                snprintf(buf, sizeof(buf), "%02u:%02u", minutes, seconds);
                lv_label_set_text(label_time, buf);
                lv_bar_set_value(bar, (int32_t)((break_stop_elapsed * 100UL) / POMODORO_BREAK_MS), LV_ANIM_OFF);
        }

        delay(LVGL_TIMER_DELAY); // CPU負荷軽減
    }

    // 到達しないが、コンパイラ警告回避用
    return false;
}

/**
 * @brief SOUND メニュー項目を実行し、環境音 BGM 選択画面を表示する.
 *
 * - BEACH / BONFIRE / RAIN の 3 種類の背景画像と BGM を選択
 * - Button1: 現在選択中の BGM の再生/停止トグル
 * - Button2: メインメニューへ戻る（BGM は停止）
 *
 * @retval true  連続実行を継続したい場合（現在は常に false）
 * @retval false メインメニューへ戻る
 */
bool execute_SOUND() {
    // ==========================================
    // ここにSOUND項目の実行内容を実装してください
    // ==========================================
    //焚火や波・雨などの集中できそうな音声を流す予定
    //encoderに合わせて背景に設定した焚火、砂浜、雨の画像を切り替え予定
    
    // --- 画面生成 ---
    lv_obj_t * sound_screen = lv_obj_create(NULL);

    lv_scr_load(sound_screen);
    set_indev_suppress_key_until_release(true);

    //画像の表示係数を画面移行時に初期化
    current_bg_idx = 0;
    // 画像オブジェクトの作成（ローラーの右側に配置）
    bg_img_obj = lv_img_create(sound_screen);
    lv_obj_align(bg_img_obj, LV_ALIGN_CENTER, 0, 0);
    lv_img_set_src(bg_img_obj, SOUND[0]);

    lv_obj_t * rect = lv_obj_create(lv_scr_act());  // 今の画面に追加
    lv_obj_set_size(rect, 120, 20);                  // 幅40px 高さ20px
    lv_obj_set_style_radius(rect, 4, 0);            //角を丸める(4px)
    lv_obj_set_style_border_width(rect, 1, 0);      //1pxの枠線を付ける
    lv_obj_set_style_border_color(rect, lv_color_black(), 0);   //枠線を黒色にする
    lv_obj_align(rect, LV_ALIGN_TOP_MID, 0, 3);

    lv_obj_set_style_bg_color(rect, lv_color_make(30, 30, 50), 0);
    lv_obj_set_style_bg_opa(rect, LV_OPA_70, 0);

    lv_obj_t * label_title = lv_label_create(rect);
    lv_label_set_text(label_title, "SELECT sound");
    lv_obj_align(label_title, LV_ALIGN_CENTER, 0, 0);

    //lv_obj_t * btn_L = lv_btn_create(sound_screen);
    //lv_obj_align(btn_L, LV_ALIGN_BOTTOM_LEFT, -10, 10);

    lv_obj_t * rect_u1 = lv_obj_create(lv_scr_act());  // 今の画面に追加
    lv_obj_set_size(rect_u1, 60, 30);                  // 幅40px 高さ20px
    lv_obj_set_style_radius(rect_u1, 4, 0);            //角を丸める(4px)
    lv_obj_set_style_border_width(rect_u1, 1, 0);      //1pxの枠線を付ける
    lv_obj_set_style_border_color(rect_u1, lv_color_black(), 0);   //枠線を黒色にする
    lv_obj_align(rect_u1, LV_ALIGN_BOTTOM_LEFT, -5, 10);
    
    lv_obj_set_style_bg_color(rect_u1, lv_color_make(30, 30, 50), 0);
    lv_obj_set_style_bg_opa(rect_u1, LV_OPA_70, 0);

    lv_obj_t * label1 = lv_label_create(rect_u1);
    lv_label_set_text(label1, "Back");
    lv_obj_align(label1, LV_ALIGN_TOP_RIGHT, 5, -10);

    //lv_obj_t * btn_R = lv_btn_create(sound_screen);
    //lv_obj_align(btn_R, LV_ALIGN_BOTTOM_RIGHT, 10, 10);
    
    
    lv_obj_t * rect_u2 = lv_obj_create(lv_scr_act());  // 今の画面に追加
    lv_obj_set_size(rect_u2, 60, 30);                  // 幅40px 高さ20px
    lv_obj_set_style_radius(rect_u2, 4, 0);            //角を丸める(4px)
    lv_obj_set_style_border_width(rect_u2, 1, 0);      //1pxの枠線を付ける
    lv_obj_set_style_border_color(rect_u2, lv_color_black(), 0);   //枠線を黒色にする
    lv_obj_align(rect_u2, LV_ALIGN_BOTTOM_RIGHT, 5, 10);

    lv_obj_set_style_bg_color(rect_u2, lv_color_make(30, 30, 50), 0);
    lv_obj_set_style_bg_opa(rect_u2, LV_OPA_70, 0);

    lv_obj_t * label2 = lv_label_create(rect_u2);
    lv_label_set_text(label2, audio_bgm_is_playing() ? "Stop" : "Start");
    lv_obj_align(label2, LV_ALIGN_TOP_LEFT, -5, -10);

    
    // エンコーダ入力グループの設定（画面ロード前に設定）
    if (g_main != nullptr && indev_encoder != nullptr) {
        lv_group_remove_all_objs(g_main);
        lv_group_add_obj(g_main, bg_img_obj);
        lv_indev_set_group(indev_encoder, g_main);  // グループをエンコーダに再設定
        /* 3. イベントを登録 */
        lv_obj_add_event_cb(bg_img_obj, bg_change_event_cb, LV_EVENT_KEY, NULL);
        lv_group_focus_obj(bg_img_obj);
        lv_group_set_editing(g_main, true);
    }


    // 親（画面/コンテナ）側のスクロールバーを無効化
    lv_obj_set_scrollbar_mode(sound_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(sound_screen, LV_OBJ_FLAG_SCROLLABLE);

    
    // 親（画面/コンテナ）側のスクロールバーを無効化
    lv_obj_set_scrollbar_mode(rect, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(rect, LV_OBJ_FLAG_SCROLLABLE);

    // ローラー側のスクロールバーを無効化（既に入っているならOK）
    lv_obj_set_scrollbar_mode(rect_u1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(rect_u1, LV_OBJ_FLAG_SCROLLABLE);
    // ローラー側のスクロールバーを無効化（既に入っているならOK）
    lv_obj_set_scrollbar_mode(rect_u2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(rect_u2, LV_OBJ_FLAG_SCROLLABLE);
    
    
    // --- ボタン状態管理 ---
    bool button1_prev = false;
    bool button2_prev = false;
    uint32_t last_button_update = 0;
    const uint32_t BUTTON_UPDATE_INTERVAL = 50;
    bool button_free = false;
    
    while (true) {
        // LVGLタイマーハンドラを呼び出す（画面更新のため）
        lv_timer_handler();
        
        // BUTTON2が押されたら終了（ローラーに戻る）
        bool button1_current = false;
        bool button2_current = false;
        
        if (ioExpander.isInitialized()) {
            // 一定間隔でボタン状態を更新（INTピンがLOWの時のみI2C通信）
            uint32_t now = millis();
            if (now - last_button_update >= BUTTON_UPDATE_INTERVAL) {
                ioExpander.updateButtonStates();
                last_button_update = now;
            }
            // キャッシュからボタン状態を取得
            button1_current = ioExpander.getButton1();
            button2_current = ioExpander.getButton2();
        }
        

        
        // Rollerからの選択時に押されていた連続検知防止
        if (!button1_current && !button2_current && !button_free) {
            button_free = true;
        }

        if (button1_current && !button1_prev && button_free) {
            // Button1: 再生・停止のトグル（表示中の画像に対応するBGM）
            bool now_playing = audio_bgm_toggle(current_bg_idx);
            lv_label_set_text(label2, now_playing ? "Stop" : "Start");
        }
        
        if (button2_current && !button2_prev) {
            // Button2: Mainに戻る → BGMを強制停止してから戻る
            audio_bgm_stop();
            save_stat_to_nvs();
            select_sound();
            lv_scr_load(main_screen);
            lv_obj_del(sound_screen);
            return false; // ローラーに戻る
        }

        
        button1_prev = button1_current;
        button2_prev = button2_current;
        delay(LVGL_TIMER_DELAY); // CPU負荷軽減
    }
    
    return false;
}

/**
 * @brief OTA画面を実行（WiFi検出→一覧→パスワード→接続→更新、完了時back_screenに戻る）
 */
static void run_ota_flow(lv_obj_t* back_screen);
static void run_dice_edit_flow(lv_obj_t* back_screen);
static void run_timer_edit_flow(lv_obj_t* back_screen);
static void run_volume_edit_flow(lv_obj_t* back_screen);
/** クレジット画面（文字が上方向に流れ、ボタンで項目選択に戻る） */
static void run_credit_screen(lv_obj_t* back_screen);

/**
 * @brief SETTING メニュー項目を実行し、設定画面を表示する.
 *
 * 構成:
 * - トップローラーで DICE CHANGE / TIMER / VOLUME / CREDIT を選択
 * - Button1 で選択中の項目のサブ画面（ダイス編集 / タイマー編集 / 音量編集 / クレジット）へ遷移
 * - Button2 で設定内容を保存してメインメニューへ戻る
 *
 * @retval true  連続実行を継続したい場合（現在は常に false）
 * @retval false メインメニューへ戻る
 */
bool execute_SETTING() {
    /* ----- 画面・UIの作成 ----- */
    lv_obj_t* setting_screen = lv_obj_create(NULL);

    lv_scr_load(setting_screen);
    set_indev_suppress_key_until_release(true);
    
    lv_obj_set_scrollbar_mode(setting_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(setting_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* トップローラー: DICE編集 / TIMER / VOLUME / クレジット を選択 */
    lv_obj_t* top_roller = lv_roller_create(setting_screen);
    lv_roller_set_options(top_roller, "DICE CHANGE\nTIMER\nVOLUME\nCREDIT", LV_ROLLER_MODE_INFINITE);
    lv_obj_align(top_roller, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_width(top_roller, 140);
    lv_obj_set_style_outline_width(top_roller, 0, LV_STATE_FOCUSED | LV_STATE_EDITED);
    lv_obj_set_style_border_width(top_roller, 0, LV_PART_SELECTED);
    


    
    // 親（画面/コンテナ）側のスクロールバーを無効化
    lv_obj_set_scrollbar_mode(setting_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(setting_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(top_roller, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(top_roller, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(top_roller, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_outline_width(top_roller, 0, LV_STATE_FOCUSED | LV_STATE_EDITED);
    lv_obj_set_style_border_width(top_roller, 0, LV_PART_SELECTED);
    lv_obj_clear_flag(top_roller, LV_OBJ_FLAG_SCROLLABLE);


    /* ----- エンコーダ入力の設定 ----- */
    if (g_main != nullptr && indev_encoder != nullptr) {
        lv_indev_reset(indev_encoder, NULL);  /* Main画面から遷移時の入力状態をクリア */
        lv_group_remove_all_objs(g_main);
        lv_group_add_obj(g_main, top_roller);
        lv_indev_set_group(indev_encoder, g_main);
        lv_group_focus_obj(top_roller);
        lv_group_set_editing(g_main, true);   /* エディットモード: エンコーダ回転でローラー選択 */
    }

    /* ----- メインループ ----- */
    bool button1_prev = false, button2_prev = false;
    uint32_t last_button_update = 0;
    const uint32_t BUTTON_UPDATE_INTERVAL = 50;
    bool button_free = false;  /* Main画面から入った直後の誤検知防止: 一度離すまで無視 */
    lv_group_set_editing(g_main, true);   /* エディットモード: エンコーダ回転でローラー選択 */

    while (true) {
        lv_timer_handler();
        bool button1_current = false, button2_current = false;

        if (ioExpander.isInitialized()) {
            uint32_t now = millis();
            if (now - last_button_update >= BUTTON_UPDATE_INTERVAL) {
                ioExpander.updateButtonStates();
                last_button_update = now;
            }
            button1_current = ioExpander.getButton1();
            button2_current = ioExpander.getButton2();
        }
        if (!button1_current && !button2_current) button_free = true;
        /* Button1: DICE編集 / TIMER / クレジット を選択して実行 */
        if (button1_current && !button1_prev && button_free) {
            int sel = lv_roller_get_selected(top_roller);
            select_sound();
            if (sel == 0) {
                button_free = false;
                run_dice_edit_flow(setting_screen);
                set_indev_suppress_key_until_release(true);
            } else if (sel == 1) {
                button_free = false;
                run_timer_edit_flow(setting_screen);
                set_indev_suppress_key_until_release(true);
            } else if (sel == 2) {
                button_free = false;
                run_volume_edit_flow(setting_screen);
                set_indev_suppress_key_until_release(true);
            } else {
                button_free = false;
                run_credit_screen(setting_screen);
                set_indev_suppress_key_until_release(true);
            }
            /* サブ画面でグループが変更されているため、top_rollerに復元 */
            if (g_main != nullptr && indev_encoder != nullptr) {
                lv_group_remove_all_objs(g_main);
                lv_group_add_obj(g_main, top_roller);
                lv_indev_set_group(indev_encoder, g_main);
                lv_group_focus_obj(top_roller);
                lv_group_set_editing(g_main, true);
            }
        }
        /* Button2: カスタムダイスを保存してMain画面に戻る */
        if (button2_current && !button2_prev && button_free) {
            save_custom_dice_to_nvs();
            select_sound();
            lv_scr_load(main_screen);
            lv_obj_del(setting_screen);
            return false;
        }

        button1_prev = button1_current;
        button2_prev = button2_current;
        delay(LVGL_TIMER_DELAY);
    }
    return false;
}

/**
 * @brief クレジット画面（文字を上方向に流し、全部見たら下から再表示。ボタンで項目選択に戻る）
 */
static void run_credit_screen(lv_obj_t* back_screen) {
    lv_obj_t* credit_screen = lv_obj_create(NULL);
    lv_scr_load(credit_screen);
    lv_obj_set_scrollbar_mode(credit_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(credit_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(credit_screen, lv_color_hex(0x101020), 0);

    /* クレジット文言（改行で複数行） */
    static const char CREDIT_TEXT[] =
        "CREDITS\n\n"
        "Project\nRan Type LCD\n\n"
        "Firmware\nVer1.0\n\n"
        "Use Library\nLVGL 8.3.4\nLovyanGFX 1.2.7\n\n"
        "Use BGM&SE\nKOUKAON-LAB\n\n"
        "---\n"
        "-Button: Back";

    lv_obj_t* label = lv_label_create(credit_screen);
    lv_label_set_text(label, CREDIT_TEXT);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, (lv_coord_t)(SCREEN_WIDTH - 16));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_update_layout(credit_screen);
    lv_coord_t label_h = lv_obj_get_height(label);
    if (label_h < 20) label_h = (lv_coord_t)SCREEN_HEIGHT;

    /* 開始位置: 画面下から登場 */
    lv_coord_t credit_y = (lv_coord_t)SCREEN_HEIGHT;
    lv_obj_set_pos(label, 8, credit_y);

    const int SCROLL_SPEED = 1;
    bool button1_prev = false, button2_prev = false;
    uint32_t last_btn = 0;
    bool button_free = false;  /* Credit遷移直後の誤検知防止: 一度離すまで無視 */

    while (true) {
        lv_timer_handler();

        if (ioExpander.isInitialized()) {
            uint32_t now = millis();
            if (now - last_btn >= 50) {
                ioExpander.updateButtonStates();
                last_btn = now;
            }
        }
        bool b1 = ioExpander.isInitialized() ? ioExpander.getButton1() : false;
        bool b2 = ioExpander.isInitialized() ? ioExpander.getButton2() : false;
        if (!b1 && !b2) button_free = true;
        if (button_free && (b2 && !button2_prev)) {
            select_sound();
            break;
        }
        button1_prev = b1;
        button2_prev = b2;

        credit_y -= (lv_coord_t)SCROLL_SPEED;
        if (credit_y + label_h < 0) {
            credit_y = (lv_coord_t)SCREEN_HEIGHT;
        }
        lv_obj_set_y(label, credit_y);

        delay(LVGL_TIMER_DELAY);
    }

    lv_scr_load(back_screen);
    lv_obj_del(credit_screen);
}

static lv_obj_t* timer_edit_label_ptr = nullptr;
static int timer_edit_is_break = 0;  /* 0=Work, 1=Break（run_timer_edit_flowで設定） */
static void timer_edit_key_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_KEY) return;
    uint32_t key = lv_event_get_key(e);
    if (timer_edit_is_break) {
        if (key == LV_KEY_RIGHT || key == LV_KEY_UP) {
            if (pomodoro_break_min < 30) pomodoro_break_min++;
        } else if (key == LV_KEY_LEFT || key == LV_KEY_DOWN) {
            if (pomodoro_break_min > 1) pomodoro_break_min--;
        }
    } else {
        if (key == LV_KEY_RIGHT || key == LV_KEY_UP) {
            if (pomodoro_work_min < 60) pomodoro_work_min++;
        } else if (key == LV_KEY_LEFT || key == LV_KEY_DOWN) {
            if (pomodoro_work_min > 1) pomodoro_work_min--;
        }
    }
    if (timer_edit_label_ptr) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", timer_edit_is_break ? (unsigned)pomodoro_break_min : (unsigned)pomodoro_work_min);
        lv_label_set_text(timer_edit_label_ptr, buf);
    }
}

/**
 * @brief VOLUME編集画面（SETTING→VOLUME）
 * 全体ボリューム(1-3)をエンコーダで変更し、NVSに保存
 */
static void run_volume_edit_flow(lv_obj_t* back_screen) {
    lv_obj_t* vol_screen = lv_obj_create(NULL);
    lv_scr_load(vol_screen);
    set_indev_suppress_key_until_release(true);

    lv_obj_set_scrollbar_mode(vol_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(vol_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* roller = lv_roller_create(vol_screen);
    lv_roller_set_options(roller, "Volume 1\nVolume 2\nVolume 3", LV_ROLLER_MODE_NORMAL);
    lv_obj_align(roller, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_width(roller, 120);
    lv_obj_set_style_outline_width(roller, 0, LV_STATE_FOCUSED | LV_STATE_EDITED);
    lv_obj_set_style_border_width(roller, 0, LV_PART_SELECTED);

    uint8_t cur_vol = audio_get_volume();
    if (cur_vol < 1 || cur_vol > 3) cur_vol = 2;
    lv_roller_set_selected(roller, (int)cur_vol - 1, LV_ANIM_OFF);

    if (g_main != nullptr && indev_encoder != nullptr) {
        lv_group_remove_all_objs(g_main);
        lv_group_add_obj(g_main, roller);
        lv_indev_set_group(indev_encoder, g_main);
        lv_group_focus_obj(roller);
        lv_group_set_editing(g_main, true);
    }

    bool button1_prev = false, button2_prev = false;
    uint32_t last_btn = 0;
    bool button_free = false;

    while (true) {
        lv_timer_handler();
        bool b1 = false, b2 = false;
        if (ioExpander.isInitialized()) {
            uint32_t now = millis();
            if (now - last_btn >= 50) {
                ioExpander.updateButtonStates();
                last_btn = now;
            }
            b1 = ioExpander.getButton1();
            b2 = ioExpander.getButton2();
        }
        if (!b1 && !b2) button_free = true;

        if (b2 && !button2_prev && button_free) {
            select_sound();
            button_free = false;
            lv_scr_load(back_screen);
            lv_obj_del(vol_screen);
            
            return;
        }
        if (b1 && !button1_prev && button_free) {
            select_sound();
            int sel = lv_roller_get_selected(roller); /* 0,1,2 -> volume 1-3 */
            uint8_t new_vol = (uint8_t)(sel + 1);
            audio_set_volume(new_vol);
            save_stat_to_nvs();
            button_free = false;
            lv_scr_load(back_screen);
            lv_obj_del(vol_screen);
            return;
        }

        button1_prev = b1;
        button2_prev = b2;
        delay(LVGL_TIMER_DELAY);
    }
}

/**
 * @brief TIMER編集画面（SETTING→TIMER）
 * 集中時間(1-60分)・休憩時間(1-30分)をエンコーダで変更し、NVSに保存
 */
static void run_timer_edit_flow(lv_obj_t* back_screen) {
    enum TimerEditState { TIMER_LIST, TIMER_EDIT_WORK, TIMER_EDIT_BREAK };
    TimerEditState state = TIMER_LIST;

    lv_obj_t* timer_screen = lv_obj_create(NULL);
    lv_scr_load(timer_screen);
    set_indev_suppress_key_until_release(true);
    
    lv_obj_set_scrollbar_mode(timer_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(timer_screen, LV_OBJ_FLAG_SCROLLABLE);

    static char roller_opts[64];
    snprintf(roller_opts, sizeof(roller_opts), "Focus (%u min)\nBreak (%u min)",
             (unsigned)pomodoro_work_min, (unsigned)pomodoro_break_min);
    lv_obj_t* roller = lv_roller_create(timer_screen);
    lv_roller_set_options(roller, roller_opts, LV_ROLLER_MODE_INFINITE);
    lv_obj_align(roller, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_width(roller, 140);
    lv_obj_set_style_outline_width(roller, 0, LV_STATE_FOCUSED | LV_STATE_EDITED);
    lv_obj_set_style_border_width(roller, 0, LV_PART_SELECTED);
    
    lv_obj_t* rect_edit = lv_obj_create(timer_screen);
    lv_obj_set_size(rect_edit, 90, 28);
    lv_obj_align(rect_edit, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(rect_edit, 4, 0);
    lv_obj_set_style_border_width(rect_edit, 1, 0);
    lv_obj_add_flag(rect_edit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(rect_edit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(rect_edit, timer_edit_key_cb, LV_EVENT_KEY, NULL);

    lv_obj_t* label_edit = lv_label_create(rect_edit);
    lv_label_set_text(label_edit, "25");
    lv_obj_align(label_edit, LV_ALIGN_CENTER, 0, 0);

    if (g_main != nullptr && indev_encoder != nullptr) {
        lv_group_remove_all_objs(g_main);
        lv_group_add_obj(g_main, roller);
        lv_indev_set_group(indev_encoder, g_main);
        lv_group_focus_obj(roller);
        lv_group_set_editing(g_main, true);
    }

    bool button1_prev = false, button2_prev = false;
    uint32_t last_btn = 0;
    bool button_free = false;

    while (true) {
        lv_timer_handler();

        bool b1 = false, b2 = false;
        if (ioExpander.isInitialized()) {
            uint32_t now = millis();
            if (now - last_btn >= 50) {
                ioExpander.updateButtonStates();
                last_btn = now;
            }
            b1 = ioExpander.getButton1();
            b2 = ioExpander.getButton2();
        }
        if (!b1 && !b2) button_free = true;

        if (state == TIMER_LIST) {
            lv_obj_add_flag(rect_edit, LV_OBJ_FLAG_HIDDEN);
            if (b2 && !button2_prev && button_free) {
                select_sound();
                lv_scr_load(back_screen);
                lv_obj_del(timer_screen);
                return;
            }
            if (b1 && !button1_prev && button_free) {
                select_sound();
                button_free = false;
                int sel = lv_roller_get_selected(roller);
                if (sel == 0) {
                    state = TIMER_EDIT_WORK;
                    timer_edit_label_ptr = label_edit;
                    timer_edit_is_break = 0;
                    lv_obj_clear_flag(rect_edit, LV_OBJ_FLAG_HIDDEN);
                    char buf[8];
                    snprintf(buf, sizeof(buf), "%u", (unsigned)pomodoro_work_min);
                    lv_label_set_text(label_edit, buf);
                    if (g_main) {
                        lv_group_remove_all_objs(g_main);
                        lv_group_add_obj(g_main, rect_edit);
                        lv_group_focus_obj(rect_edit);
                    }
                } else {
                    state = TIMER_EDIT_BREAK;
                    timer_edit_label_ptr = label_edit;
                    timer_edit_is_break = 1;
                    lv_obj_clear_flag(rect_edit, LV_OBJ_FLAG_HIDDEN);
                    char buf[8];
                    snprintf(buf, sizeof(buf), "%u", (unsigned)pomodoro_break_min);
                    lv_label_set_text(label_edit, buf);
                    if (g_main) {
                        lv_group_remove_all_objs(g_main);
                        lv_group_add_obj(g_main, rect_edit);
                        lv_group_focus_obj(rect_edit);
                    }
                }
            }
        } else if (state == TIMER_EDIT_WORK || state == TIMER_EDIT_BREAK) {
            if (b2 && !button2_prev && button_free) {
                select_sound();
                state = TIMER_LIST;
                lv_obj_add_flag(rect_edit, LV_OBJ_FLAG_HIDDEN);
                snprintf(roller_opts, sizeof(roller_opts), "Focus (%u min)\nBreak (%u min)",
                         (unsigned)pomodoro_work_min, (unsigned)pomodoro_break_min);
                lv_roller_set_options(roller, roller_opts, LV_ROLLER_MODE_INFINITE);
                if (g_main) {
                    lv_group_remove_all_objs(g_main);
                    lv_group_add_obj(g_main, roller);
                    lv_group_focus_obj(roller);
                }
                button_free = false;
            }
            if (b1 && !button1_prev && button_free) {
                select_sound();
                save_stat_to_nvs();
                state = TIMER_LIST;
                lv_obj_add_flag(rect_edit, LV_OBJ_FLAG_HIDDEN);
                snprintf(roller_opts, sizeof(roller_opts), "Focus (%u min)\nBreak (%u min)",
                         (unsigned)pomodoro_work_min, (unsigned)pomodoro_break_min);
                lv_roller_set_options(roller, roller_opts, LV_ROLLER_MODE_INFINITE);
                if (g_main) {
                    lv_group_remove_all_objs(g_main);
                    lv_group_add_obj(g_main, roller);
                    lv_group_focus_obj(roller);
                }
                button_free = false;
            }
        }

        button1_prev = b1;
        button2_prev = b2;
        delay(LVGL_TIMER_DELAY);
    }
}

/**
 * @brief DICE編集画面を実行
 *
 * フロー: リスト(C1〜C5) → Count編集(1-9) → Faces編集(1-255) → リストに戻る
 * 操作: エンコーダ=値変更, Button1=次へ/完了, Button2=戻る
 * 戻り: back_screen をロードして呼び出し元(SETTING)に復帰
 */
static void run_dice_edit_flow(lv_obj_t* back_screen) {
    enum DiceState { DICE_LIST, DICE_EDIT_COUNT, DICE_EDIT_FACES };
    DiceState state = DICE_LIST;

    /* ----- 画面・UIの作成 ----- */
    lv_obj_t* dice_screen = lv_obj_create(NULL);
    lv_scr_load(dice_screen);
    set_indev_suppress_key_until_release(true);
    lv_obj_set_scrollbar_mode(dice_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(dice_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* ローラー用オプション文字列: "2D12\n4D6\n..." 形式 */
    char custom_roller_opts[80];
    int opt_len = 0;
    for (int i = 0; i < NUM_CUSTOM_DICE; i++) {
        opt_len += snprintf(custom_roller_opts + opt_len, sizeof(custom_roller_opts) - (size_t)opt_len,
            "%s%uD%u", (i > 0) ? "\n" : "", (unsigned)g_custom_dice_count[i], (unsigned)g_custom_dice_faces[i]);
    }

    /* カスタムダイス一覧ローラー (C1〜C5) */
    lv_obj_t* roller = lv_roller_create(dice_screen);
    lv_roller_set_options(roller, custom_roller_opts, LV_ROLLER_MODE_INFINITE);
    lv_obj_align(roller, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_width(roller, 80);
    lv_obj_set_scrollbar_mode(roller, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_outline_width(roller, 0, LV_STATE_FOCUSED | LV_STATE_EDITED);
    lv_obj_set_style_border_width(roller, 0, LV_PART_SELECTED);

    lv_obj_set_scrollbar_mode(roller, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(roller, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label_title = lv_label_create(dice_screen);
    lv_label_set_text(label_title, "Custom\n  Dice");
    lv_obj_align(label_title, LV_ALIGN_TOP_RIGHT, -3, 3);

    lv_obj_t* label_edit = lv_label_create(dice_screen);

    /* 編集モード用: エンコーダでCount/Facesを増減。子のlabel_editで値を表示（非表示時は親ごと隠れる） */
    lv_obj_t* rect_edit = lv_obj_create(dice_screen);
    lv_obj_set_size(rect_edit, 100, 30);
    lv_obj_align(rect_edit, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(rect_edit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(rect_edit, lv_color_make(40, 40, 60), 0);
    lv_obj_set_scrollbar_mode(rect_edit, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_parent(label_edit, rect_edit);
    lv_obj_align(label_edit, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(label_edit, LV_OBJ_FLAG_SCROLLABLE);

    // 画像オブジェクトの作成（ローラーの右側に配置）
    lv_obj_t* image_obj_list = lv_img_create(dice_screen);
    lv_obj_set_size(image_obj_list, IMAGE_WIDTH, IMAGE_HEIGHT);
    lv_obj_align(image_obj_list, LV_ALIGN_LEFT_MID, IMAGE_ALIGN_X, IMAGE_ALIGN_Y+20);
    lv_img_set_src(image_obj_list, &DICE);

    int edit_idx = 0;
    /* エンコーダキーイベント用: edit_idx/state を参照して custom_dice_* を更新 */
    struct EditCtx { int* idx; DiceState* st; lv_obj_t* lbl; } edit_ctx;
    edit_ctx.idx = &edit_idx;
    edit_ctx.st = &state;
    edit_ctx.lbl = label_edit;

    /* Count/Faces編集時: エンコーダで値を増減し、ラベルを更新 */
    auto edit_key_cb = [](lv_event_t* e) -> void {
        if (lv_event_get_code(e) != LV_EVENT_KEY) return;
        void* ud = lv_event_get_user_data(e);
        EditCtx* ctx = (EditCtx*)ud;
        int idx = *ctx->idx;
        uint32_t key = lv_event_get_key(e);
        if (*ctx->st == DICE_EDIT_COUNT) {
            if (key == LV_KEY_RIGHT || key == LV_KEY_UP) {
                if (g_custom_dice_count[idx] < 9) g_custom_dice_count[idx]++;
            } else if (key == LV_KEY_LEFT || key == LV_KEY_DOWN) {
                if (g_custom_dice_count[idx] > 1) g_custom_dice_count[idx]--;
            }
            char buf[16];
            snprintf(buf, sizeof(buf), "%u", (unsigned)g_custom_dice_count[idx]);
            lv_label_set_text(ctx->lbl, buf);
        } else if (*ctx->st == DICE_EDIT_FACES) {
            if (key == LV_KEY_RIGHT || key == LV_KEY_UP) {
                if (g_custom_dice_faces[idx] < 255) g_custom_dice_faces[idx]++;
            } else if (key == LV_KEY_LEFT || key == LV_KEY_DOWN) {
                if (g_custom_dice_faces[idx] > 1) g_custom_dice_faces[idx]--;
            }
            char buf[16];
            snprintf(buf, sizeof(buf), "%u", (unsigned)g_custom_dice_faces[idx]);
            lv_label_set_text(ctx->lbl, buf);
        }
    };
    lv_obj_add_event_cb(rect_edit, edit_key_cb, LV_EVENT_KEY, &edit_ctx);

    /* ----- エンコーダグループ設定 ----- */
    if (g_main != nullptr && indev_encoder != nullptr) {
        lv_group_remove_all_objs(g_main);
        lv_obj_add_flag(roller, LV_OBJ_FLAG_CLICKABLE);
        lv_group_add_obj(g_main, roller);
        lv_obj_add_flag(rect_edit, LV_OBJ_FLAG_CLICKABLE);
        lv_indev_set_group(indev_encoder, g_main);
        lv_group_focus_obj(roller);
        lv_group_set_editing(g_main, true);
    }

    bool button1_prev = false, button2_prev = false;
    uint32_t last_button_update = 0;
    const uint32_t BUTTON_UPDATE_INTERVAL = 50;
    bool button_free = false;
    /* ----- メインループ ----- */
    while (true) {
        lv_timer_handler();
        bool button1_current = false, button2_current = false;

        if (ioExpander.isInitialized()) {
            uint32_t now = millis();
            if (now - last_button_update >= BUTTON_UPDATE_INTERVAL) {
                ioExpander.updateButtonStates();
                last_button_update = now;
            }
            button1_current = ioExpander.getButton1();
            button2_current = ioExpander.getButton2();
        }
        if (!button1_current && !button2_current) button_free = true;

        /* DICE_LIST: ローラーでC1〜C5を選択。Button1=編集開始, Button2=SETTINGに戻る */
        if (state == DICE_LIST) {
            if (button1_current && !button1_prev && button_free) {
                lv_obj_add_flag(image_obj_list, LV_OBJ_FLAG_HIDDEN);  /* 開始後は画像を非表示 */
                edit_idx = lv_roller_get_selected(roller);
                if (edit_idx >= NUM_CUSTOM_DICE) edit_idx = 0;
                state = DICE_EDIT_COUNT;
                lv_obj_add_flag(roller, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(rect_edit, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(label_title, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(label_title, "Count (1-9)");
                char buf[16];
                snprintf(buf, sizeof(buf), "%u", (unsigned)g_custom_dice_count[edit_idx]);
                lv_label_set_text(label_edit, buf);
                lv_obj_align(label_edit, LV_ALIGN_CENTER, 0, 0);
                if (g_main && indev_encoder) {
                    lv_group_remove_all_objs(g_main);
                    lv_group_add_obj(g_main, rect_edit);
                    lv_group_focus_obj(rect_edit);
                }
                select_sound();
            }
            if (button2_current && !button2_prev && button_free) {
                save_custom_dice_to_nvs();
                select_sound();
                lv_obj_clear_flag(image_obj_list, LV_OBJ_FLAG_HIDDEN);
                button_free = false;
                lv_scr_load(back_screen);
                lv_obj_del(dice_screen);
                return;
            }
        /* DICE_EDIT_COUNT: エンコーダで1〜9を変更。Button1=Facesへ, Button2=リストに戻る */
        } else if (state == DICE_EDIT_COUNT) {
            if (button1_current && !button1_prev && button_free) {
                state = DICE_EDIT_FACES;
                lv_label_set_text(label_title, "Faces (1-255)");
                char buf[16];
                snprintf(buf, sizeof(buf), "%u", (unsigned)g_custom_dice_faces[edit_idx]);
                lv_label_set_text(label_edit, buf);
                lv_obj_align(label_edit, LV_ALIGN_CENTER, 0, 0);
                select_sound();
            }
            if (button2_current && !button2_prev && button_free) {
                state = DICE_LIST;
                lv_obj_clear_flag(roller, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(rect_edit, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(label_title, "Custom\n  Dice");
                lv_obj_clear_flag(image_obj_list, LV_OBJ_FLAG_HIDDEN);
                opt_len = 0;
                for (int i = 0; i < NUM_CUSTOM_DICE; i++) {
                    opt_len += snprintf(custom_roller_opts + opt_len, sizeof(custom_roller_opts) - (size_t)opt_len,
                        "%s%uD%u", (i > 0) ? "\n" : "", (unsigned)g_custom_dice_count[i], (unsigned)g_custom_dice_faces[i]);
                }
                lv_roller_set_options(roller, custom_roller_opts, LV_ROLLER_MODE_INFINITE);
                if (g_main && indev_encoder) {
                    lv_group_remove_all_objs(g_main);
                    lv_group_add_obj(g_main, roller);
                    lv_group_focus_obj(roller);
                }
                select_sound();
            }
        /* DICE_EDIT_FACES: エンコーダで1〜255を変更。Button1=完了, Button2=キャンセル */
        } else if (state == DICE_EDIT_FACES) {
            if (button1_current && !button1_prev && button_free) {
                state = DICE_LIST;
                lv_obj_clear_flag(roller, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(rect_edit, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(label_title, "Custom\n  Dice");
                lv_obj_clear_flag(image_obj_list, LV_OBJ_FLAG_HIDDEN);
                opt_len = 0;
                for (int i = 0; i < NUM_CUSTOM_DICE; i++) {
                    opt_len += snprintf(custom_roller_opts + opt_len, sizeof(custom_roller_opts) - (size_t)opt_len,
                        "%s%uD%u", (i > 0) ? "\n" : "", (unsigned)g_custom_dice_count[i], (unsigned)g_custom_dice_faces[i]);
                }
                lv_roller_set_options(roller, custom_roller_opts, LV_ROLLER_MODE_INFINITE);
                if (g_main && indev_encoder) {
                    lv_group_remove_all_objs(g_main);
                    lv_group_add_obj(g_main, roller);
                    lv_group_focus_obj(roller);
                }
                select_sound();
            }
            if (button2_current && !button2_prev && button_free) {
                state = DICE_LIST;
                lv_obj_clear_flag(roller, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(rect_edit, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(label_title, "Custom\n  Dice");
                lv_obj_clear_flag(image_obj_list, LV_OBJ_FLAG_HIDDEN);
                opt_len = 0;
                for (int i = 0; i < NUM_CUSTOM_DICE; i++) {
                    opt_len += snprintf(custom_roller_opts + opt_len, sizeof(custom_roller_opts) - (size_t)opt_len,
                        "%s%uD%u", (i > 0) ? "\n" : "", (unsigned)g_custom_dice_count[i], (unsigned)g_custom_dice_faces[i]);
                }
                lv_roller_set_options(roller, custom_roller_opts, LV_ROLLER_MODE_INFINITE);
                if (g_main && indev_encoder) {
                    lv_group_remove_all_objs(g_main);
                    lv_group_add_obj(g_main, roller);
                    lv_group_focus_obj(roller);
                }
                select_sound();
            }
        }

        button1_prev = button1_current;
        button2_prev = button2_current;
        delay(LVGL_TIMER_DELAY);
    }
}

/**
 * 電流量が大きすぎて電池の状態で実行すると落ちるので未実装
 * @brief OTA画面を実行
 *
 * フロー: WiFi一覧 → パスワード入力(文字/完了/中止/削除) → 接続 → OTA更新
 * 操作: エンコーダ=選択・文字変更, Button1=決定, Button2=WiFi選択に戻る
 * 戻り: back_screen をロードして呼び出し元(SETTING)に復帰
 */
/*
static void run_ota_flow(lv_obj_t* back_screen) {
    enum OtaState {
        OTA_WIFI_LIST,       // WiFi一覧表示・選択 
        OTA_PASSWORD_ACTION, // 文字/完了/中止/削除 の選択 
        OTA_PASSWORD_CHAR,   // 文字入力（エンコーダで1文字選択） 
        OTA_CONNECTING,      // WiFi接続中 
        OTA_UPDATING         // ファームウェアダウンロード中 
    };
    OtaState state = OTA_WIFI_LIST;
    static bool ota_conn_done = false;
    ota_conn_done = false;

    /// ----- 画面・UIの作成 ----- 
    lv_obj_t* ota_screen = lv_obj_create(NULL);
    lv_scr_load(ota_screen);
    set_indev_suppress_key_until_release(true);
    lv_obj_set_scrollbar_mode(ota_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ota_screen, LV_OBJ_FLAG_SCROLLABLE);

    // タイトル（状態に応じて "WiFi Scan...", "Select WiFi", "Password" 等に変更） 
    lv_obj_t* ota_label_title = lv_label_create(ota_screen);
    lv_label_set_text(ota_label_title, "WiFi Scan...");
    lv_obj_align(ota_label_title, LV_ALIGN_TOP_MID, 0, 3);

    // WiFi一覧 or パスワード入力時の選択表示用
    lv_obj_t* ota_roller = lv_roller_create(ota_screen);
    lv_obj_align(ota_roller, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_width(ota_roller, 140);
    lv_obj_set_scrollbar_mode(ota_roller, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* ota_label_value = lv_label_create(ota_screen);
    lv_obj_align(ota_label_value, LV_ALIGN_CENTER, 0, 0);

    // パスワード入力時: エンコーダで選択するフォーカス対象。子のota_label_valueで表示
    lv_obj_t* ota_rect_focus = lv_obj_create(ota_screen);
    lv_obj_set_size(ota_rect_focus, 100, 30);
    lv_obj_align(ota_rect_focus, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(ota_rect_focus, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(ota_rect_focus, lv_color_make(40, 40, 60), 0);
    lv_obj_set_scrollbar_mode(ota_rect_focus, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_parent(ota_label_value, ota_rect_focus);
    lv_obj_align(ota_label_value, LV_ALIGN_CENTER, 0, 0);

    // ----- WiFiスキャン ----- 
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks();
    char wifi_list_buf[512];
    int wifi_list_len = 0;
    wifi_list_buf[0] = '\0';
    int wifi_count = (n > OTA_WIFI_SCAN_MAX) ? OTA_WIFI_SCAN_MAX : (n > 0 ? n : 0);
    for (int i = 0; i < wifi_count; i++) {
        wifi_list_len += snprintf(wifi_list_buf + wifi_list_len, sizeof(wifi_list_buf) - (size_t)wifi_list_len,
            "%s%s", (i > 0) ? "\n" : "", WiFi.SSID(i).c_str());
    }
    if (wifi_count == 0) {
        snprintf(wifi_list_buf, sizeof(wifi_list_buf), "(No networks)");
    }

    lv_roller_set_options(ota_roller, wifi_list_buf, LV_ROLLER_MODE_INFINITE);
    lv_label_set_text(ota_label_title, "Select WiFi");

    // パスワード入力用: 使用可能な文字セット 
    static const char OTA_CHARS[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%&*-_=+ ";
    static const int OTA_CHARS_N = (int)(sizeof(OTA_CHARS) - 1);

    char ota_password[OTA_PASSWORD_MAX_LEN + 1];
    int ota_password_len = 0;
    ota_password[0] = '\0';

    int ota_action_sel = 0;   // PASSWORD_ACTION時の選択: 0=文字, 1=完了, 2=中止, 3=削除 
    int ota_char_sel = 0;     // PASSWORD_CHAR時の文字インデックス 

    // ----- エンコーダグループ設定 ----- 
    // WiFi一覧時は ota_roller のみ登録。ota_rect_focus を同時に入れるとフォーカスが奪われエンコーダが効かない 
    if (g_main != nullptr && indev_encoder != nullptr) {
        lv_group_remove_all_objs(g_main);
        lv_obj_add_flag(ota_roller, LV_OBJ_FLAG_CLICKABLE);
        lv_group_add_obj(g_main, ota_roller);
        lv_obj_add_flag(ota_rect_focus, LV_OBJ_FLAG_CLICKABLE);
        lv_indev_set_group(indev_encoder, g_main);
        lv_group_focus_obj(ota_roller);
        lv_group_set_editing(g_main, true);
    }

    // パスワード入力のキーコールバック用コンテキスト 
    struct OtaPwdCtx {
        OtaState* st;
        lv_obj_t* lbl;
        char* pwd;
        int* pwd_len;
        int* action_sel;
        int* char_sel;
    } pwd_ctx;
    pwd_ctx.st = &state;
    pwd_ctx.lbl = ota_label_value;
    pwd_ctx.pwd = ota_password;
    pwd_ctx.pwd_len = &ota_password_len;
    pwd_ctx.action_sel = &ota_action_sel;
    pwd_ctx.char_sel = &ota_char_sel;

    // パスワード入力時: エンコーダで action_sel または char_sel を変更し、ラベルを更新 
    auto ota_pwd_key_cb = [](lv_event_t* e) -> void {
        if (lv_event_get_code(e) != LV_EVENT_KEY) return;
        void* ud = lv_event_get_user_data(e);
        OtaPwdCtx* ctx = (OtaPwdCtx*)ud;
        uint32_t key = lv_event_get_key(e);
        if (*ctx->st == OTA_PASSWORD_ACTION) {
            // 文字/完了/中止/削除 をエンコーダで選択 
            if (key == LV_KEY_RIGHT || key == LV_KEY_UP) {
                *ctx->action_sel = (*ctx->action_sel + 1) % 4;
            } else if (key == LV_KEY_LEFT || key == LV_KEY_DOWN) {
                *ctx->action_sel = (*ctx->action_sel + 3) % 4;
            }
            const char* opts[] = {"PASS", "OK", "STOP", "DEL"};
            char buf[32];
            snprintf(buf, sizeof(buf), "%s", opts[*ctx->action_sel % 4]);
            lv_label_set_text(ctx->lbl, buf);
        } else if (*ctx->st == OTA_PASSWORD_CHAR) {
            // 1文字をエンコーダで選択 
            if (key == LV_KEY_RIGHT || key == LV_KEY_UP) {
                *ctx->char_sel = (*ctx->char_sel + 1) % OTA_CHARS_N;
            } else if (key == LV_KEY_LEFT || key == LV_KEY_DOWN) {
                *ctx->char_sel = (*ctx->char_sel + OTA_CHARS_N - 1) % OTA_CHARS_N;
            }
            char buf[8];
            char c = OTA_CHARS[*ctx->char_sel];
            snprintf(buf, sizeof(buf), "%c", c == ' ' ? '_' : c);
            lv_label_set_text(ctx->lbl, buf);
        }
    };
    lv_obj_add_event_cb(ota_rect_focus, ota_pwd_key_cb, LV_EVENT_KEY, &pwd_ctx);

    bool button1_prev = false, button2_prev = false;
    uint32_t last_button_update = 0;
    const uint32_t BUTTON_UPDATE_INTERVAL = 50;
    bool button_free = false;
    int wifi_selected_idx = 0;
    String wifi_ssid_selected;

    // ----- メインループ ----- 
    while (true) {
        lv_timer_handler();
        bool button1_current = false, button2_current = false;

        if (ioExpander.isInitialized()) {
            uint32_t now = millis();
            if (now - last_button_update >= BUTTON_UPDATE_INTERVAL) {
                ioExpander.updateButtonStates();
                last_button_update = now;
            }
            button1_current = ioExpander.getButton1();
            button2_current = ioExpander.getButton2();
        }
        if (!button1_current && !button2_current) button_free = true;

        // OTA_WIFI_LIST: WiFi選択。Button1=パスワード入力へ, Button2=SETTINGに戻る 
        if (state == OTA_WIFI_LIST) {
            if (button1_current && !button1_prev && button_free && wifi_count > 0) {
                wifi_selected_idx = lv_roller_get_selected(ota_roller);
                if (wifi_selected_idx >= wifi_count) wifi_selected_idx = 0;
                wifi_ssid_selected = WiFi.SSID(wifi_selected_idx);
                state = OTA_PASSWORD_ACTION;
                lv_obj_add_flag(ota_roller, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(ota_rect_focus, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(ota_label_title, "Password");
                ota_action_sel = 0;
                lv_label_set_text(ota_label_value, "PASS");
                if (g_main && indev_encoder) {
                    lv_group_remove_all_objs(g_main);
                    lv_group_add_obj(g_main, ota_rect_focus);
                    lv_group_focus_obj(ota_rect_focus);
                }
                select_sound();
            }
            if (button2_current && !button2_prev && button_free) {
                select_sound();
                lv_scr_load(back_screen);
                lv_obj_del(ota_screen);
                return;
            }
        // OTA_PASSWORD_ACTION: 文字/完了/中止/削除 を選択。Button1で決定
        } else if (state == OTA_PASSWORD_ACTION) {
            if (button1_current && !button1_prev && button_free) {
                if (ota_action_sel == 0) {
                    state = OTA_PASSWORD_CHAR;
                    ota_char_sel = 0;
                    char buf[8];
                    snprintf(buf, sizeof(buf), "%c", OTA_CHARS[0] == ' ' ? '_' : OTA_CHARS[0]);
                    lv_label_set_text(ota_label_value, buf);
                    select_sound();
                } else if (ota_action_sel == 1) {
                    // 完了: パスワード確定して接続開始 
                    ota_password[ota_password_len] = '\0';
                    state = OTA_CONNECTING;
                    lv_label_set_text(ota_label_title, "Connecting...");
                    lv_label_set_text(ota_label_value, "");
                    select_sound();
                } else if (ota_action_sel == 2) {
                    // 中止: WiFi選択画面に戻る 
                    state = OTA_WIFI_LIST;
                    lv_obj_clear_flag(ota_roller, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(ota_rect_focus, LV_OBJ_FLAG_HIDDEN);
                    lv_roller_set_options(ota_roller, wifi_list_buf, LV_ROLLER_MODE_INFINITE);
                    lv_label_set_text(ota_label_title, "Select WiFi");
                    if (g_main && indev_encoder) {
                        lv_group_remove_all_objs(g_main);
                        lv_group_add_obj(g_main, ota_roller);
                        lv_group_focus_obj(ota_roller);
                        lv_group_set_editing(g_main, true);
                    }
                    select_sound();
                } else if (ota_action_sel == 3) {
                    // 削除: 最後の1文字を削除 
                    if (ota_password_len > 0) {
                        ota_password_len--;
                        ota_password[ota_password_len] = '\0';
                    }
                    ota_action_sel = 0;
                    lv_label_set_text(ota_label_value, "PASS");
                    select_sound();
                }
            }
        // OTA_PASSWORD_CHAR: エンコーダで文字選択。Button1で確定してパスワードに追加 
        } else if (state == OTA_PASSWORD_CHAR) {
            if (button1_current && !button1_prev && button_free) {
                char c = OTA_CHARS[ota_char_sel];
                if (ota_password_len < OTA_PASSWORD_MAX_LEN) {
                    ota_password[ota_password_len++] = c;
                    ota_password[ota_password_len] = '\0';
                }
                state = OTA_PASSWORD_ACTION;
                ota_action_sel = 0;
                lv_label_set_text(ota_label_value, "PASS");
                select_sound();
            }
        // OTA_CONNECTING: WiFi接続試行（最大15秒）。成功→OTA_UPDATING, 失敗→WiFi一覧 
        } else if (state == OTA_CONNECTING) {
            if (!ota_conn_done) {
                ota_conn_done = true;
                WiFi.begin(wifi_ssid_selected.c_str(), ota_password);
                unsigned long t0 = millis();
                while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 15000) {
                    lv_timer_handler();
                    delay(50);
                }
            }
            if (WiFi.status() == WL_CONNECTED) {
                state = OTA_UPDATING;
                lv_label_set_text(ota_label_title, "OTA Update...");
            } else {
                ota_conn_done = false;
                lv_label_set_text(ota_label_title, "Connect Failed");
                lv_label_set_text(ota_label_value, "Retry");
                state = OTA_WIFI_LIST;
                lv_obj_clear_flag(ota_roller, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ota_rect_focus, LV_OBJ_FLAG_HIDDEN);
                lv_roller_set_options(ota_roller, wifi_list_buf, LV_ROLLER_MODE_INFINITE);
                if (g_main && indev_encoder) {
                    lv_group_remove_all_objs(g_main);
                    lv_group_add_obj(g_main, ota_roller);
                    lv_group_focus_obj(ota_roller);
                    lv_group_set_editing(g_main, true);
                }
                delay(2000);
            }
        // OTA_UPDATING: config.h の OTA_FIRMWARE_URL からファームウェアを取得。成功→再起動 
        } else if (state == OTA_UPDATING) {
            WiFiClientSecure client;
            client.setInsecure();
            HTTPUpdate httpUpdate;
            t_httpUpdate_return ret = httpUpdate.update(client, OTA_FIRMWARE_URL);
            if (ret == HTTP_UPDATE_OK) {
                lv_label_set_text(ota_label_title, "OK! Reboot...");
                delay(1500);
                ESP.restart();
            } else {
                lv_label_set_text(ota_label_title, "OTA Failed");
                lv_label_set_text(ota_label_value, "Back");
                state = OTA_WIFI_LIST;
                lv_obj_clear_flag(ota_roller, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ota_rect_focus, LV_OBJ_FLAG_HIDDEN);
                lv_roller_set_options(ota_roller, wifi_list_buf, LV_ROLLER_MODE_INFINITE);
                if (g_main && indev_encoder) {
                    lv_group_remove_all_objs(g_main);
                    lv_group_add_obj(g_main, ota_roller);
                    lv_group_focus_obj(ota_roller);
                    lv_group_set_editing(g_main, true);
                }
                delay(2000);
            }
        }

        button1_prev = button1_current;
        button2_prev = button2_current;
        delay(LVGL_TIMER_DELAY);
    }
}
*/