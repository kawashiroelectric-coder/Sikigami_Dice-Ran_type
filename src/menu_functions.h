#pragma once

#include <lvgl.h>

/**
 * @file menu_functions.h
 * @brief ローラー各項目の実行内容を定義するモジュール
 * 
 * 各項目（STAT, GAME, DICE, TIMER, ROLLER, SETTING）の実行内容を
 * 関数として定義します。実装はmenu_functions.cppで行います。
 */

// 各項目の実行関数の宣言
// 戻り値: true = 継続実行、false = 終了（ローラーに戻る）

/**
 * @brief STAT項目の実行内容
 * @return true = 継続実行、false = 終了（ローラーに戻る）
 */
bool execute_STAT();

/**
 * @brief DICE項目の実行内容
 * @return true = 継続実行、false = 終了（ローラーに戻る）
 */
bool execute_DICE();

/**
 * @brief TIMER項目の実行内容
 * @return true = 継続実行、false = 終了（ローラーに戻る）
 */
bool execute_TIMER();

/**
 * @brief ROLLER項目の実行内容
 * @return true = 継続実行、false = 終了（ローラーに戻る）
 */
bool execute_SOUND();

/**
 * @brief SETTING項目の実行内容
 * @return true = 継続実行、false = 終了（ローラーに戻る）
 */
bool execute_SETTING();

/**
 * @brief カスタムダイス5種を不揮発メモリから読み込み（setupで呼ぶ）
 */
void load_custom_dice_from_nvs(void);

/**
 * @brief STAT表示データを不揮発メモリから読み込み（setupで呼ぶ）
 */
void load_stat_from_nvs(void);

/** Main→他画面遷移直後のボタン押下を無視する（main.cpp で実装） */
void set_indev_suppress_key_until_release(bool suppress);

/**
 * @brief GAMEハイスコア報告（game.cppから呼ぶ。snake_len>0 or stg_score>0）
 */
void game_report_high_score(int snake_len, uint32_t stg_score);
