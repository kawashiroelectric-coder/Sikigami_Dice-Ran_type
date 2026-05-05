#pragma once

#include <lvgl.h>
#include <stdint.h>

/**
 * @file game.h
 * @brief ゲームモジュール（スネーク / 横向きSTG）
 *
 * GAME項目選択時のメニュー表示と各ゲームの実行を担当します。
 */

/**
 * @brief STG用エンコーダ累積値を取得してクリア（mainで実装）
 * エンコーダの生のステップ数（正=下方向、負=上方向）を返す
 */
int32_t get_encoder_stg_delta(void);

/**
 * @brief GAME項目の実行内容
 * ローラーでスネークゲーム / 横向きSTG を選択して実行
 * @return true = 継続実行、false = 終了（ローラーに戻る）
 */
bool execute_GAME();
