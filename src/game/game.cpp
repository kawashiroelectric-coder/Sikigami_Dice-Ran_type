/**
 * @file game.cpp
 * @brief GAMEモジュール実装（スネーク / 横向きSTG）
 */

#include <Arduino.h>
#include <lvgl.h>
#include "game.h"
#include "config.h"
#include "include/PCA9539.h"
#include "include/sound.h"
#include "menu_functions.h"

extern PCA9539 ioExpander;
extern lv_obj_t* main_screen;
extern lv_group_t* g_main;
extern lv_indev_t* indev_encoder;
extern "C"{
extern const lv_img_dsc_t STG_CH;
extern const lv_img_dsc_t FAIRY;
extern const lv_img_dsc_t TREE_ROADroad;
extern const lv_img_dsc_t BGGAME;
extern const lv_img_dsc_t BGSNAKE;
extern const lv_img_dsc_t RAINBOW;
}

static void select_sound_game(void) {
    audio_se_play(2000, 100);
    audio_se_play(0, 5);
    audio_se_play(1000, 100);
    audio_se_play(0, 5);
}

/* エンコーダ方向用（キーコールバックから更新） */
static volatile int g_snake_dir_input = 0;   /* -1=左回転, +1=右回転 */
static volatile int g_stg_move_input = 0;    /* -1=上, +1=下 */

// SNAKE の移動間隔 (ms) の上限・下限
static const uint32_t kSnakeMoveIntervalMaxMs = 400;
static const uint32_t kSnakeMoveIntervalMinMs = 220;

/**
 * @brief 現在の長さに応じた SNAKE の移動間隔 (ms) を取得する.
 *
 * 長さが大きくなるほど 300ms から 180ms まで直線的に短くなり、
 * 難易度が徐々に上がるようにする。
 *
 * @param length 現在のスネークの長さ
 * @param max_length 長さの理論上の最大値（補間の終点）
 * @return 移動間隔 (ms)
 */
static uint32_t get_snake_move_interval_ms(int length, int max_length) {
    if (length <= 0 || max_length <= 1) {
        return kSnakeMoveIntervalMaxMs;
    }
    if (length <= 3) {
        return kSnakeMoveIntervalMaxMs;
    }
    if (length >= max_length) {
        return kSnakeMoveIntervalMinMs;
    }
    const uint32_t range = kSnakeMoveIntervalMaxMs - kSnakeMoveIntervalMinMs;
    const int span = max_length - 3;
    const int pos = length - 3;
    uint32_t delta = (range * (uint32_t)pos) / (uint32_t)span;
    uint32_t interval = kSnakeMoveIntervalMaxMs - delta;
    if (interval < kSnakeMoveIntervalMinMs) interval = kSnakeMoveIntervalMinMs;
    if (interval > kSnakeMoveIntervalMaxMs) interval = kSnakeMoveIntervalMaxMs;
    return interval;
}

static void snake_key_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_KEY) return;
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_LEFT || key == LV_KEY_DOWN) g_snake_dir_input = -1;
    else if (key == LV_KEY_RIGHT || key == LV_KEY_UP) g_snake_dir_input = 1;
}

static void stg_key_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_KEY) return;
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_UP || key == LV_KEY_LEFT) g_stg_move_input = -1;
    else if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT) g_stg_move_input = 1;
}

/* ============================================================================
 * スネークゲーム
 * グリッド 8x4 (セル 20x20)、エンコーダで移動方向、壁/自機衝突でゲームオーバー
 * ============================================================================ */
static void run_snake(lv_obj_t* back_screen) {
    const int GW = 16, GH = 8;           /* グリッド 8x4 */
    const int CS = 10;                  /* セルサイズ 20px */
    const int MAX_LEN = 128;
    int snake_x[MAX_LEN], snake_y[MAX_LEN];
    int len = 3;
    int dir = 0;  /* 0=右, 1=下, 2=左, 3=上 */
    int food_x, food_y;
    bool game_over = false;
    uint32_t last_move = 0;

    g_snake_dir_input = 0;

    lv_obj_t* game_screen = lv_obj_create(NULL);
    lv_scr_load(game_screen);
    lv_obj_set_scrollbar_mode(game_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(game_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_set_style_bg_img_src(lv_scr_act(), &BGSNAKE, 0);
    lv_obj_set_style_bg_img_recolor(lv_scr_act(), lv_color_black(), 0);
    lv_obj_set_style_bg_img_recolor_opa(lv_scr_act(), LV_OPA_80, 0);

    /* エンコーダ入力用フォーカス対象（透明・全画面） */
    lv_obj_t* focus_area = lv_obj_create(game_screen);
    lv_obj_set_size(focus_area, 160, 80);
    lv_obj_set_pos(focus_area, 0, 0);
    lv_obj_set_style_bg_opa(focus_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(focus_area, 0, 0);
    lv_obj_add_flag(focus_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(focus_area, snake_key_cb, LV_EVENT_KEY, NULL);
    if (g_main && indev_encoder)
    {
        lv_group_remove_all_objs(g_main);
        lv_group_add_obj(g_main, focus_area);
        lv_indev_set_group(indev_encoder, g_main);
        lv_group_focus_obj(focus_area);
        lv_group_set_editing(g_main, true);
    }

    /* グリッド線（縦横）※lv_line_set_pointsはアドレスのみ保存するため static 必須 */
    lv_obj_set_style_bg_color(game_screen, lv_color_hex(0x101820), 0);
    static lv_point_t vert_pts[GW + 1][2];
    static lv_point_t horz_pts[GH + 1][2];
    for (int i = 0; i <= GW; i++)
    {
        vert_pts[i][0].x = vert_pts[i][1].x = (lv_coord_t)(i * CS);
        vert_pts[i][0].y = 0;
        vert_pts[i][1].y = (lv_coord_t)(GH * CS);
        lv_obj_t* line = lv_line_create(game_screen);
        lv_line_set_points(line, vert_pts[i], 2);
        lv_obj_set_style_line_width(line, 1, 0);
        lv_obj_set_style_line_color(line, lv_color_hex(0x304050), 0);
    }
    for (int j = 0; j <= GH; j++)
    {
        horz_pts[j][0].x = 0;
        horz_pts[j][1].x = (lv_coord_t)(GW * CS);
        horz_pts[j][0].y = horz_pts[j][1].y = (lv_coord_t)(j * CS);
        lv_obj_t* line = lv_line_create(game_screen);
        lv_line_set_points(line, horz_pts[j], 2);
        lv_obj_set_style_line_width(line, 1, 0);
        lv_obj_set_style_line_color(line, lv_color_hex(0x304050), 0);
    }

    /* スネーク・エサ用タイル（lv_objで表示） */
    static lv_obj_t* tiles[GW * GH];
    for (int i = 0; i < GW * GH; i++)
    {
        tiles[i] = lv_obj_create(game_screen);
        lv_obj_set_size(tiles[i], CS - 2, CS - 2);
        lv_obj_set_style_radius(tiles[i], 2, 0);
        lv_obj_set_style_border_width(tiles[i], 0, 0);
        lv_obj_add_flag(tiles[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* エサ専用（RAINBOW画像） */
    lv_obj_t* food_img = lv_img_create(game_screen);
    lv_img_set_src(food_img, &RAINBOW);
    lv_obj_set_size(food_img, CS - 2, CS - 2);
    lv_obj_add_flag(food_img, LV_OBJ_FLAG_HIDDEN);

    auto set_tile = [&](int gx, int gy, bool visible, bool is_food, bool is_head) {
        int i = gy * GW + gx;
        if (i < 0 || i >= GW * GH) return;
        lv_obj_clear_flag(tiles[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(tiles[i], gx * CS + 1, gy * CS + 1);
        if (is_food)
        {
            lv_obj_add_flag(tiles[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(food_img, gx * CS + 1, gy * CS + 1);
            lv_obj_clear_flag(food_img, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(food_img, LV_OBJ_FLAG_HIDDEN);
            if (is_head)
            {
                //lv_obj_set_style_bg_color(tiles[i], lv_color_hex(0xFFFF00), 0);
                lv_obj_set_style_bg_color(tiles[i], lv_color_hex(0xB64A3F), 0);
                lv_obj_set_style_border_width(tiles[i], 1, 0);
                lv_obj_set_style_border_color(tiles[i], lv_color_hex(0xED1C24), 0);
            }
            else
            {
                //lv_obj_set_style_bg_color(tiles[i], lv_color_hex(0x00AA00), 0);
                lv_obj_set_style_bg_color(tiles[i], lv_color_hex(0x74797C), 0);
                lv_obj_set_style_border_width(tiles[i], 1, 0);
                lv_obj_set_style_border_color(tiles[i], lv_color_hex(0x003E59), 0);
            }
        }
        if (!visible) lv_obj_add_flag(tiles[i], LV_OBJ_FLAG_HIDDEN);
    };
    
    /* デバッグ用表示（画面上部左: 時間連動 + 撃破ボーナス） */
    //lv_obj_t* lbl_score = lv_label_create(game_screen);
    //lv_obj_align(lbl_score, LV_ALIGN_TOP_RIGHT, -2, 2);
    //lv_obj_set_style_text_color(lbl_score, lv_color_hex(0xFFFFFF), 0);

    /* 初期配置 */
    snake_x[0] = 2; snake_y[0] = 2;
    snake_x[1] = 1; snake_y[1] = 2;
    snake_x[2] = 0; snake_y[2] = 2;
    food_x = 6; food_y = 2;

    /* タイトル/スコア */
    //lv_obj_t* lbl = lv_label_create(game_screen);
    //lv_label_set_text(lbl, "SNAKE");

    bool button1_prev = false, button2_prev = false;
    uint32_t last_btn = 0;

    while (!game_over)
    {
        lv_timer_handler();

        /* エンコーダで方向変更（90度ずつ） */
        if (g_snake_dir_input != 0)
        {
            dir = (dir + g_snake_dir_input + 4) % 4;
            g_snake_dir_input = 0;
        }

        uint32_t now = millis();
        if (ioExpander.isInitialized() && now - last_btn >= 50)
        {
            ioExpander.updateButtonStates();
            last_btn = now;
        }
        bool b1 = ioExpander.isInitialized() ? ioExpander.getButton1() : false;
        bool b2 = ioExpander.isInitialized() ? ioExpander.getButton2() : false;
        if (b2 && !button2_prev) break;  /* Button2 で戻る */
        button2_prev = b2;

        /* 移動タイミング（長さに応じて 300ms→180ms まで短縮） */
        uint32_t move_interval_ms = get_snake_move_interval_ms(len, MAX_LEN);
        if (now - last_move >= move_interval_ms)
        {
            last_move = now;
            int nx = snake_x[0], ny = snake_y[0];
            if (dir == 0) nx++; else if (dir == 1) ny++; else if (dir == 2) nx--; else ny--;
            if (nx < 0 || nx >= GW || ny < 0 || ny >= GH)
            {
                game_over = true;
                break;
            }
            for (int i = 0; i < len; i++)
            {
                if (snake_x[i] == nx && snake_y[i] == ny)
                {
                    game_over = true;
                    break;
                }
            }
            /* シフト */
            for (int i = len; i > 0; i--)
            {
                snake_x[i] = snake_x[i-1];
                snake_y[i] = snake_y[i-1];
            }
            snake_x[0] = nx; snake_y[0] = ny;
            if (nx == food_x && ny == food_y)
            {
                len++;
                if (len >= MAX_LEN) len = MAX_LEN - 1;
                do
                {
                    food_x = random(GW);
                    food_y = random(GH);
                }
                while (food_x == snake_x[0] && food_y == snake_y[0]);
            }
        }

        /* 描画 */
        for (int i = 0; i < GW * GH; i++)
            lv_obj_add_flag(tiles[i], LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < len; i++)
            set_tile(snake_x[i], snake_y[i], true, false, (i == 0));
        set_tile(food_x, food_y, true, true, false);

        

        //デバッグの更新
        //lv_label_set_text_fmt(lbl_score, "TIME:%lu", (unsigned long)move_interval_ms);


        //lv_label_set_text_fmt(lbl, "SNAKE L:%d", len);
        button1_prev = b1;
        delay(10);
    }

    /* GAME OVER表示: 中央に四角形とlen表示、ボタンで戻る */
    if (game_over)
    {
        game_report_high_score(len, 0);
        lv_obj_t* rect_go = lv_obj_create(game_screen);
        lv_obj_set_size(rect_go, 100, 40);
        lv_obj_align(rect_go, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(rect_go, lv_color_hex(0x202040), 0);
        lv_obj_set_style_border_width(rect_go, 2, 0);
        lv_obj_set_style_border_color(rect_go, lv_color_hex(0xFF4040), 0);

        lv_obj_t* lbl_go = lv_label_create(rect_go);
        lv_label_set_text_fmt(lbl_go, "SCORE:%d", len);
        lv_obj_align(lbl_go, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_color(lbl_go, lv_color_hex(0xFFFFFF), 0);
        

            
        // 親（画面/コンテナ）側のスクロールバーを無効化
        lv_obj_set_scrollbar_mode(rect_go, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(rect_go, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(lbl_go, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(lbl_go, LV_OBJ_FLAG_SCROLLABLE);

        bool go_b1_prev = false, go_b2_prev = false;
        bool go_button_free = false;
        while (true)
        {
            lv_timer_handler();
            bool go_b1 = false, go_b2 = false;
            if (ioExpander.isInitialized())
            {
                ioExpander.updateButtonStates();
                go_b1 = ioExpander.getButton1();
                go_b2 = ioExpander.getButton2();
            }
            if (!go_b1 && !go_b2) go_button_free = true;
            if (go_button_free && ((go_b1 && !go_b1_prev) || (go_b2 && !go_b2_prev)))
            {
                //select_sound_game();
                break;
            }
            go_b1_prev = go_b1;
            go_b2_prev = go_b2;
            delay(20);
        }
    }

    lv_scr_load(back_screen);
    lv_obj_del(game_screen);
}

/* ============================================================================
 * 横向きSTG（シューティングゲーム）
 * 自機左側、上下移動（エンコーダ）、Button1で射撃、敵が右から複数体出現
 * ============================================================================ */
#define STG_MAX_ENEMIES  5
#define STG_MAX_BULLETS  2
#define STG_ENEMY_SPAWN_MS  1200

static void run_stg(lv_obj_t* back_screen) {
    const int PX = 8, PY_INIT = 34;      /* 自機初期位置 */
    const int PW = 12, PH = 12;         /* 自機サイズ */
    const int BW = 4, BH = 4;           /* 弾サイズ */
    const int EW = 8, EH = 8;           /* 敵サイズ */
    int py = PY_INIT;
    int bullet_x[STG_MAX_BULLETS], bullet_y[STG_MAX_BULLETS];
    bool bullet_active[STG_MAX_BULLETS];
    for (int i = 0; i < STG_MAX_BULLETS; i++) { bullet_x[i] = -100; bullet_y[i] = 0; bullet_active[i] = false; }
    int enemy_x[STG_MAX_ENEMIES], enemy_y[STG_MAX_ENEMIES];
    bool enemy_active[STG_MAX_ENEMIES];
    int enemy_line_target_y[STG_MAX_ENEMIES]; /* 敵から伸びる棒の終点Y（上=0 or 下=SCREEN_HEIGHT） */
    uint32_t last_shot = 0;
    uint32_t last_enemy_spawn = 0;

    for (int i = 0; i < STG_MAX_ENEMIES; i++)
    {
        enemy_active[i] = false;
        enemy_x[i] = 160;
        enemy_y[i] = 40;
        enemy_line_target_y[i] = SCREEN_HEIGHT;
    }

    g_stg_move_input = 0;

    lv_obj_t* game_screen = lv_obj_create(NULL);
    lv_scr_load(game_screen);
    lv_obj_set_scrollbar_mode(game_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(game_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(game_screen, lv_color_hex(0x001030), 0);


    lv_obj_set_style_bg_img_src(lv_scr_act(), &TREE_ROADroad, 0);
    
    lv_obj_set_style_bg_img_recolor(lv_scr_act(), lv_color_black(), 0);
    lv_obj_set_style_bg_img_recolor_opa(lv_scr_act(), LV_OPA_60, 0);


    /* エンコーダ入力用フォーカス対象 */
    lv_obj_t* focus_area = lv_obj_create(game_screen);
    lv_obj_set_size(focus_area, 160, 80);
    lv_obj_set_pos(focus_area, 0, 0);
    lv_obj_set_style_bg_opa(focus_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(focus_area, 0, 0);
    lv_obj_add_flag(focus_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(focus_area, stg_key_cb, LV_EVENT_KEY, NULL);
    if (g_main && indev_encoder)
    {
        lv_group_remove_all_objs(g_main);
        lv_group_add_obj(g_main, focus_area);
        lv_indev_set_group(indev_encoder, g_main);
        lv_group_focus_obj(focus_area);
        lv_group_set_editing(g_main, true);
    }

    lv_obj_t* player = lv_img_create(game_screen);
    lv_img_set_src(player, &STG_CH);
    lv_obj_set_size(player, PW, PH);

    lv_obj_t* bullet[STG_MAX_BULLETS];
    for (int i = 0; i < STG_MAX_BULLETS; i++)
    {
        bullet[i] = lv_obj_create(game_screen);
        lv_obj_set_size(bullet[i], BW, BH);
        lv_obj_set_style_radius(bullet[i], 0, 0);
        lv_obj_set_style_bg_color(bullet[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bullet[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(bullet[i], 0, LV_PART_MAIN);
        lv_obj_set_style_outline_width(bullet[i], 0, LV_PART_MAIN);
        lv_obj_add_flag(bullet[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t* enemy_obj[STG_MAX_ENEMIES];
    for (int i = 0; i < STG_MAX_ENEMIES; i++)
    {
        enemy_obj[i] = lv_img_create(game_screen);
        lv_img_set_src(enemy_obj[i], &FAIRY);
        lv_obj_add_flag(enemy_obj[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* 敵から画面下までの赤い線（当たり判定なし・見た目のみ） */
    lv_obj_t* enemy_line[STG_MAX_ENEMIES];
    static lv_point_t line_pts[STG_MAX_ENEMIES][2];
    for (int i = 0; i < STG_MAX_ENEMIES; i++)
    {
        enemy_line[i] = lv_line_create(game_screen);
        lv_obj_set_style_line_width(enemy_line[i], 1, 0);
        lv_obj_set_style_line_color(enemy_line[i], lv_color_hex(0xFF0000), 0);
        lv_obj_add_flag(enemy_line[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* スコア表示（画面上部左: 時間連動 + 撃破ボーナス） */
    lv_obj_t* lbl_score = lv_label_create(game_screen);
    lv_obj_align(lbl_score, LV_ALIGN_TOP_RIGHT, -2, 2);
    lv_obj_set_style_text_color(lbl_score, lv_color_hex(0xFFFFFF), 0);

    /* GAME OVER表示用（中央四角形・最初は非表示） */
    lv_obj_t* rect_go = lv_obj_create(game_screen);
    lv_obj_set_size(rect_go, 100, 40);
    lv_obj_align(rect_go, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(rect_go, lv_color_hex(0x202040), 0);
    lv_obj_set_style_border_width(rect_go, 2, 0);
    lv_obj_set_style_border_color(rect_go, lv_color_hex(0xFF4040), 0);
    lv_obj_add_flag(rect_go, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* lbl_go = lv_label_create(rect_go);
    lv_obj_align(lbl_go, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(lbl_go, lv_color_hex(0xFFFFFF), 0);

    
    // 親（画面/コンテナ）側のスクロールバーを無効化
    lv_obj_set_scrollbar_mode(rect_go, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(rect_go, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(lbl_go, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(lbl_go, LV_OBJ_FLAG_SCROLLABLE);

    uint32_t game_start_ms = millis();
    uint32_t kill_bonus = 0;
    bool game_over = false;

    bool button1_prev = false, button2_prev = false;
    bool go_button_free = false;
    uint32_t last_btn = 0;

    while (true)
    {
        lv_timer_handler();
        uint32_t now = millis();

        if (ioExpander.isInitialized() && now - last_btn >= 50)
        {
            ioExpander.updateButtonStates();
            last_btn = now;
        }
        bool b1 = ioExpander.isInitialized() ? ioExpander.getButton1() : false;
        bool b2 = ioExpander.isInitialized() ? ioExpander.getButton2() : false;
        if (game_over)
        {
            if (!b1 && !b2) go_button_free = true;
            if (go_button_free && ((b1 && !button1_prev) || (b2 && !button2_prev)))
            {
                //select_sound_game();
                break;
            }
            button1_prev = b1;
            button2_prev = b2;
            delay(20);
            continue;
        }
        if (b2 && !button2_prev) break;

        /* 射撃（空いている弾スロットがあれば発射・連射間隔300ms） */
        if (b1 && !button1_prev && now - last_shot > 300)
        {
            for (int i = 0; i < STG_MAX_BULLETS; i++)
            {
                if (!bullet_active[i])
                {
                    bullet_active[i] = true;
                    bullet_x[i] = PX + 14;
                    bullet_y[i] = py + 4;
                    last_shot = now;
                    break;
                }
            }
        }

        /* 自機移動（エンコーダの累積値で上下・encoder_acc相当の生値を使用） */
        py += get_encoder_stg_delta() * 3;   /* 3px/step ≒ 1クリック(17step)で約51px */
        g_stg_move_input = 0;
        if (py < 2) py = 2;
        if (py > 80 - 14) py = 80 - 14;

        lv_obj_set_pos(player, PX, py);

        /* 弾移動 */
        for (int i = 0; i < STG_MAX_BULLETS; i++)
        {
            if (bullet_active[i])
            {
                bullet_x[i] += 6;
                lv_obj_clear_flag(bullet[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_pos(bullet[i], bullet_x[i], bullet_y[i]);
                if (bullet_x[i] > 160) bullet_active[i] = false;
            }
            else
            {
                lv_obj_add_flag(bullet[i], LV_OBJ_FLAG_HIDDEN);
            }
        }

        /* 敵スポーン: 一定間隔で空きスロットに1体出現 */
        if (now - last_enemy_spawn >= STG_ENEMY_SPAWN_MS)
        {
            for (int i = 0; i < STG_MAX_ENEMIES; i++)
            {
                if (!enemy_active[i])
                {
                    enemy_x[i] = 155;
                    enemy_y[i] = 10 + random(60);
                    /* 棒の向きを上下ランダムに決定（敵ごとに固定） */
                    enemy_line_target_y[i] = (random(2) == 0) ? 0 : SCREEN_HEIGHT;
                    enemy_active[i] = true;
                    last_enemy_spawn = now;
                    break;
                }
            }
        }

        /* 敵移動・画面外で非アクティブ化 */
        for (int i = 0; i < STG_MAX_ENEMIES; i++)
        {
            if (!enemy_active[i])
            {
                lv_obj_add_flag(enemy_obj[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(enemy_line[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            enemy_x[i] -= 2;
            lv_obj_clear_flag(enemy_obj[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(enemy_obj[i], enemy_x[i], enemy_y[i]);
            /* 敵から伸びる赤い線を更新（上下ランダム） */
            int lx = enemy_x[i] + EW / 2;
            line_pts[i][0].x = (lv_coord_t)lx;
            line_pts[i][0].y = (lv_coord_t)(enemy_y[i] + EH / 2);
            line_pts[i][1].x = (lv_coord_t)lx;
            line_pts[i][1].y = (lv_coord_t)enemy_line_target_y[i];
            lv_line_set_points(enemy_line[i], line_pts[i], 2);
            lv_obj_clear_flag(enemy_line[i], LV_OBJ_FLAG_HIDDEN);
            if (enemy_x[i] < -EW)
            {
                enemy_active[i] = false;
            }
        }

        /* 自機 vs 敵・赤線：接触でGAME OVER */
        for (int i = 0; i < STG_MAX_ENEMIES; i++)
        {
            if (!enemy_active[i]) continue;
            /* 敵本体との接触 */
            if (PX + PW > enemy_x[i] && PX < enemy_x[i] + EW &&
                py + PH > enemy_y[i] && py < enemy_y[i] + EH)
            {
                game_over = true;
                break;
            }
            /* 赤線（敵から上下どちらかへ伸びる縦線）との接触 */
            int lx = enemy_x[i] + EW / 2;
            int y0 = enemy_y[i] + EH / 2;
            int y1 = enemy_line_target_y[i];
            int ymin = (y0 < y1) ? y0 : y1;
            int ymax = (y0 > y1) ? y0 : y1;
            if (lx >= PX && lx <= PX + PW && (py + PH) >= ymin && py <= ymax)
            {
                game_over = true;
                break;
            }
        }
        if (game_over)
        {
            /* 画面中央に四角形とスコア表示 */
            uint32_t total_score = (now - game_start_ms) / 100 + kill_bonus;
            game_report_high_score(0, total_score);
            lv_label_set_text_fmt(lbl_go, "SCORE:%lu", (unsigned long)total_score);
            lv_obj_clear_flag(rect_go, LV_OBJ_FLAG_HIDDEN);

            audio_se_play(400, 200);
            delay(20);
            continue;
        }

        /* 弾と敵の当たり判定（1発で1体のみ・2発それぞれで判定） */
        for (int bi = 0; bi < STG_MAX_BULLETS; bi++)
        {
            if (!bullet_active[bi]) continue;
            for (int i = 0; i < STG_MAX_ENEMIES; i++)
            {
                if (!enemy_active[i]) continue;
                if (bullet_x[bi] + BW >= enemy_x[i] && bullet_x[bi] <= enemy_x[i] + EW &&
                    bullet_y[bi] + BH >= enemy_y[i] && bullet_y[bi] <= enemy_y[i] + EH)
                {
                    bullet_active[bi] = false;
                    enemy_active[i] = false;
                    kill_bonus += 50;  /* 撃破ボーナス */
                    audio_se_play(1200, 50);
                    break;
                }
            }
        }

        /* スコア更新: 経過時間(10/秒) + 撃破ボーナス */
        uint32_t elapsed_ms = now - game_start_ms;
        uint32_t time_score = elapsed_ms / 100;
        uint32_t total_score = time_score + kill_bonus;
        lv_label_set_text_fmt(lbl_score, "SCORE:%lu", (unsigned long)total_score);

        button1_prev = b1;
        button2_prev = b2;
        delay(20);
    }

    lv_scr_load(back_screen);
    lv_obj_del(game_screen);
}

/* ============================================================================
 * execute_GAME: GAMEメニュー（スネーク/STG選択）→ 選択して実行
 * ============================================================================ */
bool execute_GAME() {
    lv_obj_t* game_menu_screen = lv_obj_create(NULL);
    lv_scr_load(game_menu_screen);
    set_indev_suppress_key_until_release(true);
    lv_obj_set_scrollbar_mode(game_menu_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(game_menu_screen, LV_OBJ_FLAG_SCROLLABLE);

    //背景画像
    lv_obj_set_style_bg_img_src(lv_scr_act(), &BGGAME, 0);

    lv_obj_t* top_roller = lv_roller_create(game_menu_screen);
    lv_roller_set_options(top_roller, "CENTIPEDO\nRUN!RAN", LV_ROLLER_MODE_INFINITE);
    lv_obj_align(top_roller, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_width(top_roller, 140);
    lv_obj_set_scrollbar_mode(top_roller, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(top_roller, LV_OPA_70, 0);   // 70%透明
    lv_obj_clear_flag(top_roller, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(top_roller, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_outline_width(top_roller, 0, LV_STATE_FOCUSED | LV_STATE_EDITED);
    lv_obj_set_style_border_width(top_roller, 0, LV_PART_SELECTED);

    
    // 親（画面/コンテナ）側のスクロールバーを無効化
    lv_obj_set_scrollbar_mode(game_menu_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(game_menu_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(top_roller, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(top_roller, LV_OBJ_FLAG_SCROLLABLE);

    if (g_main != nullptr && indev_encoder != nullptr)
    {
        lv_indev_reset(indev_encoder, NULL);
        lv_group_remove_all_objs(g_main);
        lv_group_add_obj(g_main, top_roller);
        lv_indev_set_group(indev_encoder, g_main);
        lv_group_focus_obj(top_roller);
        lv_group_set_editing(g_main, true);
    }

    bool button1_prev = false, button2_prev = false;
    uint32_t last_button_update = 0;
    const uint32_t BUTTON_UPDATE_INTERVAL = 50;
    bool button_free = false;

    while (true)
    {
        lv_timer_handler();
        bool button1_current = false, button2_current = false;

        if (ioExpander.isInitialized())
        {
            uint32_t now = millis();
            if (now - last_button_update >= BUTTON_UPDATE_INTERVAL)
            {
                ioExpander.updateButtonStates();
                last_button_update = now;
            }
            button1_current = ioExpander.getButton1();
            button2_current = ioExpander.getButton2();
        }
        if (!button1_current && !button2_current) button_free = true;

        if (button1_current && !button1_prev && button_free)
        {
            int sel = lv_roller_get_selected(top_roller);
            select_sound_game();
            if (sel == 0)
            {
                run_snake(game_menu_screen);
                set_indev_suppress_key_until_release(true);
                select_sound_game();
                button_free = false;
            }
            else
            {
                run_stg(game_menu_screen);
                set_indev_suppress_key_until_release(true);
                select_sound_game();
                button_free = false;
            }
            if (g_main != nullptr && indev_encoder != nullptr)
            {
                lv_group_remove_all_objs(g_main);
                lv_group_add_obj(g_main, top_roller);
                lv_indev_set_group(indev_encoder, g_main);
                lv_group_focus_obj(top_roller);
                lv_group_set_editing(g_main, true);
            }
        }

        if (button2_current && !button2_prev && button_free)
        {
            select_sound_game();
            lv_scr_load(main_screen);
            lv_obj_del(game_menu_screen);
            return false;
        }

        button1_prev = button1_current;
        button2_prev = button2_current;
        delay(LVGL_TIMER_DELAY);
    }
    return false;
}
