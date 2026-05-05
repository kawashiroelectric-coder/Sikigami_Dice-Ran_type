#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

/**
 * @file PCA9539.h
 * @brief PCA9539 16ビットI/Oエキスパンダのドライバークラス
 * 
 * PCA9539は2つの8ビットポート（ポート0、ポート1）を持つI2C I/Oエキスパンダです。
 * ポート0: LED用（出力）
 * ポート1: ボタン用（入力）
 */
class PCA9539 {
public:
    /**
     * @brief コンストラクタ
     * @param i2c_addr I2Cアドレス（デフォルト: config.hのPCA9539_I2C_ADDR）
     */
    PCA9539(uint8_t i2c_addr = PCA9539_I2C_ADDR);

    /**
     * @brief 初期化
     * @param sda I2C SDAピン番号（デフォルト: config.hのI2C_SDA）
     * @param scl I2C SCLピン番号（デフォルト: config.hのI2C_SCL）
     * @param int_pin 割り込みピン番号（デフォルト: config.hのI2C_INT_PIN、未使用の場合は-1）
     * @return 初期化成功時true、失敗時false
     */
    bool begin(int sda = I2C_SDA, int scl = I2C_SCL, int int_pin = I2C_INT_PIN);

    /**
     * @brief デバイスの存在確認
     * @return デバイスが応答する場合true
     */
    bool isConnected();

    /**
     * @brief ポート0（LED用）のピン状態を設定
     * @param pin ピン番号（0-7）
     * @param state true=LOW（LED点灯）、false=HIGH（LED消灯）
     * @return 成功時true、失敗時false
     */
    bool setLED(uint8_t pin, bool state);

    /**
     * @brief LED1の状態を設定
     * @param state true=LOW（LED点灯）、false=HIGH（LED消灯）
     */
    bool setLED1(bool state);

    /**
     * @brief LED2の状態を設定
     * @param state true=LOW（LED点灯）、false=HIGH（LED消灯）
     */
    bool setLED2(bool state);

    /**
     * @brief LED3の状態を設定
     * @param state true=LOW（LED点灯）、false=HIGH（LED消灯）
     */
    bool setLED3(bool state);

    /**
     * @brief ポート1（ボタン用）のピン状態を読み取り
     * @param pin ピン番号（0-7）
     * @return true=LOW（押されている）、false=HIGH（離されている）
     */
    bool readButton(uint8_t pin);

    /**
     * @brief BUTTON1の状態を読み取り
     * @return true=LOW（押されている）、false=HIGH（離されている）
     */
    bool readButton1();

    /**
     * @brief BUTTON2の状態を読み取り
     * @return true=LOW（押されている）、false=HIGH（離されている）
     */
    bool readButton2();

    /**
     * @brief ポート0全体の状態を読み取り
     * @return ポート0の8ビット値
     */
    uint8_t readPort0();

    /**
     * @brief ポート1全体の状態を読み取り
     * @return ポート1の8ビット値
     */
    uint8_t readPort1();

    /**
     * @brief ポート0全体の状態を設定
     * @param value 8ビット値
     * @return 成功時true、失敗時false
     */
    bool writePort0(uint8_t value);

    /**
     * @brief ポート1全体の状態を設定
     * @param value 8ビット値
     * @return 成功時true、失敗時false
     */
    bool writePort1(uint8_t value);

    /**
     * @brief 割り込みピンの状態を読み取り
     * @return true=割り込み発生、false=割り込みなし
     */
    bool readInterrupt();

    /**
     * @brief エラーフラグを取得
     * @return 最後の操作でエラーが発生した場合true
     */
    bool hasError() const { return _error; }

    /**
     * @brief エラーフラグをクリア
     */
    void clearError() { _error = false; }

    /**
     * @brief 初期化済みかどうかを確認
     * @return 初期化済みの場合true
     */
    bool isInitialized() const { return _initialized; }

    /**
     * @brief ボタン状態を更新（INTピンがLOWの時のみI2C通信を行う）
     * @return 更新成功時true、失敗時false
     */
    bool updateButtonStates();

    /**
     * @brief BUTTON1の状態を取得（キャッシュから）
     * @return true=LOW（押されている）、false=HIGH（離されている）
     */
    bool getButton1() const;

    /**
     * @brief BUTTON2の状態を取得（キャッシュから）
     * @return true=LOW（押されている）、false=HIGH（離されている）
     */
    bool getButton2() const;

private:
    // PCA9539レジスタアドレス
    enum Register {
        REG_INPUT_PORT0 = 0x00,      // 入力ポート0（読み取り専用）
        REG_INPUT_PORT1 = 0x01,      // 入力ポート1（読み取り専用）
        REG_OUTPUT_PORT0 = 0x02,     // 出力ポート0
        REG_OUTPUT_PORT1 = 0x03,     // 出力ポート1
        REG_POLARITY_INV_PORT0 = 0x04, // 極性反転ポート0
        REG_POLARITY_INV_PORT1 = 0x05, // 極性反転ポート1
        REG_CONFIG_PORT0 = 0x06,     // 設定ポート0（0=出力、1=入力）
        REG_CONFIG_PORT1 = 0x07      // 設定ポート1（0=出力、1=入力）
    };

    uint8_t _i2c_addr;      // I2Cアドレス
    int _int_pin;            // 割り込みピン
    bool _error;             // エラーフラグ
    bool _initialized;       // 初期化済みフラグ
    uint8_t _port0_state;    // ポート0の現在の状態（キャッシュ）
    uint8_t _port1_state;    // ポート1の現在の状態（キャッシュ）
    bool _button1_state;     // BUTTON1の状態（キャッシュ）
    bool _button2_state;      // BUTTON2の状態（キャッシュ）

    /**
     * @brief I2Cレジスタに書き込み
     * @param reg レジスタアドレス
     * @param value 書き込む値
     * @return 成功時true、失敗時false
     */
    bool writeRegister(Register reg, uint8_t value);

    /**
     * @brief I2Cレジスタから読み取り
     * @param reg レジスタアドレス
     * @param value 読み取った値を格納する変数へのポインタ
     * @return 成功時true、失敗時false
     */
    bool readRegister(Register reg, uint8_t* value);

    /**
     * @brief 読み取りエラー時にPort0（LED）の状態を再書き込みしてHi-Z復帰を防ぐ
     */
    void restoreOutputState();
};
