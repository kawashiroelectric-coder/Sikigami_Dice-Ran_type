#pragma once

#include <lvgl.h>

/**
 * @file menu_functions.h
 * @brief ローラー各項目の実行内容を定義するモジュール
 * 
 * 各項目（STAT, GAME, DICE, TIMER, ROLLER, SETTING）の実行内容を
 * 関数として定義します。実装はmenu_functions.cppで行います。
 */

/**
 * @brief STAT項目の実行内容
 * @return true = 継続実行、false = 終了（ローラーに戻る）
 */
bool execute_OP();
/**
 * @brief STAT項目の実行内容
 * @return true = 継続実行、false = 終了（ローラーに戻る）
 */
bool execute_ROGO();