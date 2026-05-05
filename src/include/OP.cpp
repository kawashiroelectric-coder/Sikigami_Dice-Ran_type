#include "OP.h"
#include <Arduino.h>
#include <lvgl.h>
#include "menu_functions.h"
#include "config.h"
#include "include/PCA9539.h"
#include "include/sound.h"

// PCA9539の外部参照（main.cppで定義）
extern PCA9539 ioExpander;
// メイン画面（実行画面から戻る際に再ロードする）
extern lv_obj_t* main_screen;
// エンコーダ入力グループ（main.cppで定義）
extern lv_group_t* g_main;
extern lv_indev_t* indev_encoder;

lv_obj_t* op_screen = nullptr;
lv_obj_t* ROGO_screen = nullptr;

bool is_animating = true;

//画像データ
extern "C" {
    extern const lv_img_dsc_t ROTATION;
    extern const lv_img_dsc_t ROGO;
    extern const lv_img_dsc_t BGIMAGE;
}

static void anim_x_cb(void * var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, v);
    lv_obj_set_y((lv_obj_t *)var,0);
}
static void anim_x_cb1(void * var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, v);
}
static void anim_y_cb(void * var, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)var, v);
}
static void anim_y_cb1(void * var, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)var, v);
}

/* 画面を揺らすためのコールバック（translateでラッパーをずらす） */
static void anim_shake_cb1(void* var, int32_t v) {
    lv_obj_t* obj = (lv_obj_t*)var;
    lv_obj_set_style_translate_x(obj, v, LV_PART_MAIN);
    lv_obj_set_style_translate_y(obj, v, LV_PART_MAIN);
}
static void anim_shake_cb2(void* var, int32_t v) {
    lv_obj_t* obj = (lv_obj_t*)var;
    lv_obj_set_style_translate_x(obj, v, LV_PART_MAIN);
    lv_obj_set_style_translate_y(obj, v, LV_PART_MAIN);
}
static void anim_shake_cb3(void* var, int32_t v) {
    lv_obj_t* obj = (lv_obj_t*)var;
    lv_obj_set_style_translate_x(obj, v, LV_PART_MAIN);
    lv_obj_set_style_translate_y(obj, v, LV_PART_MAIN);
}



/* 画像を回転させるためのコールバック (v8.3用) */
static void anim_rotate_cb(void * var, int32_t v)
{
    /* v8.3では lv_img_set_angle を使用します */
    /* vは 0 ～ 3600 (0.1度単位) */
    lv_img_set_angle((lv_obj_t *)var, (int16_t)v);
}

/* アニメーションが終わった時に呼ばれる関数 */
static void op_end_cb(lv_anim_t * a)
{
    // アニメーションが終わった時の処理
    // 例えば、グローバルなフラグを false にするなど
    is_animating = false;
    lv_scr_load(main_screen);
    // もしオブジェクトを消したいなら
    lv_obj_del(op_screen); 
}

// 音を鳴らす
static void sound_on_cb(lv_anim_t * a) {
    // HIT_SE のPCM効果音を再生
    audio_se_play_hit();
    //ledcAttachPin(BUZZER_PIN, 0); // ピンをLEDCに紐付け
    //ledcWriteTone(BUZZER_PIN, 800); // 800Hzで鳴らす
}

// 音を止める
static void sound_off_cb(lv_anim_t * a) {
    //ledcWriteTone(BUZZER_PIN, 0);   // 0Hzで消音
}




/**
 * @brief ROGO項目の実行内容
 */
bool execute_ROGO() {
    int timer = 0;
    ROGO_screen = lv_obj_create(NULL);

    lv_scr_load(ROGO_screen);
    lv_obj_t* image_obj_ROGO = lv_img_create(ROGO_screen);
    lv_img_set_src(image_obj_ROGO, &ROGO);
    lv_obj_set_size(image_obj_ROGO, 160, 80);
    lv_obj_align(image_obj_ROGO, LV_ALIGN_CENTER, 0, 0);

    timer = millis();
    while (true) {
        // LVGLタイマーハンドラを呼び出す（画面更新のため）
        lv_timer_handler();
        
        if (millis() - timer > 2000) {
            // ボタンが押された瞬間を検出
            // 画面をクリーンアップ
            lv_scr_load(main_screen);
            lv_obj_del(ROGO_screen);
            return false;
        }
    }
    return false;
}



/**
 * @brief OP項目の実行内容
 */
bool execute_OP() {

    // ==========================================
    // ここにOP項目の実行内容を実装してください
    // ==========================================
    
    op_screen = lv_obj_create(NULL);
    lv_scr_load(op_screen);

    /* 揺らし用ラッパー（画面ルートへのtranslateは効かないことがあるため、子コンテナを揺らす） */
    lv_obj_t* shake_wrap = lv_obj_create(op_screen);
    lv_obj_set_size(shake_wrap, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(shake_wrap, 0, 0);
    lv_obj_set_scrollbar_mode(shake_wrap, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(shake_wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(shake_wrap, 0, 0);
    lv_obj_set_style_border_width(shake_wrap, 0, 0);
    lv_obj_set_style_pad_all(shake_wrap, 0, 0);
    lv_obj_set_style_bg_opa(shake_wrap, LV_OPA_TRANSP, 0);

    //ここに背景画像
    lv_obj_t* image_obj_opbg = lv_img_create(shake_wrap);
    lv_img_set_src(image_obj_opbg, &BGIMAGE);
    lv_obj_set_size(image_obj_opbg, 160, 80);
    lv_obj_align(image_obj_opbg, LV_ALIGN_CENTER, 0, 0);

    //回転する藍様
    lv_obj_t* image_obj_op = lv_img_create(shake_wrap);
    lv_img_set_src(image_obj_op, &ROTATION);
    lv_obj_set_size(image_obj_op, IMAGE_WIDTH, IMAGE_HEIGHT);
    lv_obj_align(image_obj_op, LV_ALIGN_LEFT_MID, IMAGE_ALIGN_X, 0);

    // 親（画面/コンテナ）側のスクロールバーを無効化
    lv_obj_set_scrollbar_mode(op_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(op_screen, LV_OBJ_FLAG_SCROLLABLE);



    /* --- タイムラインの作成 --- */
    lv_anim_timeline_t * nt = lv_anim_timeline_create();

    /* --- A: 回転 (1回だけ。タイムライン全体をループさせればOK) --- */
    lv_anim_t a_rot;
    lv_anim_init(&a_rot);
    lv_anim_set_var(&a_rot, image_obj_op);
    lv_anim_set_exec_cb(&a_rot, anim_rotate_cb);
    lv_anim_set_values(&a_rot, 0, 180000);
    lv_anim_set_time(&a_rot, 1600); // 全体の長さに合わせる

    /* --- B: 右移動 --- */
    lv_anim_t a_x_fwd;
    lv_anim_init(&a_x_fwd);
    lv_anim_set_var(&a_x_fwd, image_obj_op);
    lv_anim_set_exec_cb(&a_x_fwd, anim_x_cb);
    lv_anim_set_values(&a_x_fwd, -40, 120);
    lv_anim_set_time(&a_x_fwd, 800);

    /* --- C: 反射（左・上） --- */
    lv_anim_t a_x_back;
    lv_anim_init(&a_x_back);
    lv_anim_set_var(&a_x_back, image_obj_op);
    lv_anim_set_exec_cb(&a_x_back, anim_x_cb1);
    lv_anim_set_values(&a_x_back, 120, 90);
    lv_anim_set_time(&a_x_back, 800);
    lv_anim_set_path_cb(&a_x_back,lv_anim_path_ease_in);

    lv_anim_t a_y_up;
    lv_anim_init(&a_y_up);
    lv_anim_set_var(&a_y_up, image_obj_op);
    lv_anim_set_exec_cb(&a_y_up, anim_y_cb);
    lv_anim_set_values(&a_y_up, 0, -25);
    lv_anim_set_time(&a_y_up, 400);
    lv_anim_set_path_cb(&a_y_up,lv_anim_path_ease_out);

    lv_anim_t a_y_down;
    lv_anim_init(&a_y_down);
    lv_anim_set_var(&a_y_down, image_obj_op);
    lv_anim_set_exec_cb(&a_y_down, anim_y_cb1);
    lv_anim_set_time(&a_y_down, 400);
    lv_anim_set_values(&a_y_down, -25, 0);
    lv_anim_set_path_cb(&a_y_down,lv_anim_path_ease_in);

    /* 揺らす対象：ラッパーコンテナ（画面ルートはtranslateが効かないことがあるため） */
    lv_obj_t* shake_target = shake_wrap;

    // 振動1: 右・下へ（0 -> 4px）
    lv_anim_t s1;
    lv_anim_init(&s1);
    lv_anim_set_var(&s1, shake_target);
    lv_anim_set_exec_cb(&s1, anim_shake_cb1);
    lv_anim_set_values(&s1, 0, 4);
    lv_anim_set_time(&s1, 40);

    // 振動2: 左・上へ（4 -> -4px）
    lv_anim_t s2;
    lv_anim_init(&s2);
    lv_anim_set_var(&s2, shake_target);
    lv_anim_set_exec_cb(&s2, anim_shake_cb2);
    lv_anim_set_values(&s2, 4, -4);
    lv_anim_set_time(&s2, 40);

    // 振動3: 中央へ戻る（-4 -> 0px）
    lv_anim_t s3;
    lv_anim_init(&s3);
    lv_anim_set_var(&s3, shake_target);
    lv_anim_set_exec_cb(&s3, anim_shake_cb3);
    lv_anim_set_values(&s3, -4, 0);
    lv_anim_set_time(&s3, 40);

    //効果音の定義
    // 音出し用の「空のアニメーション」を作る（値を変化させない）
    lv_anim_t s_on,s_off;
    
    lv_anim_init(&s_on);
    lv_anim_set_var(&s_on, NULL);          // 対象物はいらない
    lv_anim_set_exec_cb(&s_on, (lv_anim_exec_xcb_t)sound_on_cb); // 鳴らす
    lv_anim_set_time(&s_on, 0);            // 一瞬で実行

    
    lv_anim_init(&s_off);
    lv_anim_set_var(&s_off, NULL);          // 対象物はいらない
    lv_anim_set_exec_cb(&s_off, (lv_anim_exec_xcb_t)sound_off_cb); // 鳴らす
    lv_anim_set_time(&s_off, 0);            // 一瞬で実行
    
    

    /* ★重要：最後のアニメーションにだけ ready_cb を入れる */
    lv_anim_set_ready_cb(&a_y_down, op_end_cb);

    
    // 画像が衝突する「800ms」付近に集中させて登録する
    uint32_t hit = 800; 

    lv_anim_timeline_add(nt, 0, &a_rot);
    lv_anim_timeline_add(nt, 0, &a_x_fwd);
    lv_anim_timeline_add(nt, 800, &a_x_back);
    lv_anim_timeline_add(nt, 800, &s1);   // ぶつかった瞬間から振動
    lv_anim_timeline_add(nt, 700, &s_on);      // 衝突した瞬間にSE
    //lv_anim_timeline_add(nt, 900, &s_off);      // 衝突した瞬間にSE
    lv_anim_timeline_add(nt, 840, &s2);
    lv_anim_timeline_add(nt, 880, &s3);
    lv_anim_timeline_add(nt, 800, &a_y_up);
    lv_anim_timeline_add(nt, 1200, &a_y_down);

    /* --- 再生開始 --- */
    lv_anim_timeline_start(nt);


    while(true){
        // LVGLタイマーハンドラを呼び出す（画面更新のため）
        lv_timer_handler();
        if(is_animating == false){
        
        return false; // ローラーに戻る
        }
    }
}