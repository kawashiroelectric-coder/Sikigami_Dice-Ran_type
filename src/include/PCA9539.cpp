#include "include/PCA9539.h"

/**
 * @brief コンストラクタ
 */
PCA9539::PCA9539(uint8_t i2c_addr) 
    : _i2c_addr(i2c_addr), _int_pin(-1), _error(false), _initialized(false), 
      _port0_state(0xFF), _port1_state(0xFF), _button1_state(false), _button2_state(false) {
}

/**
 * @brief 初期化
 */
bool PCA9539::begin(int sda, int scl, int int_pin) {
    _error = false;
    _int_pin = int_pin;

    // I2C初期化
    Wire.begin(sda, scl);
    Wire.setClock(100000); // 100kHz（標準速度）
    Wire.setTimeout(100);  // I2C通信のタイムアウトを100msに設定

    // デバイスの存在確認
    if (!isConnected()) {
        _error = true;
        _initialized = false;
#if DEBUG_ENABLED
        Serial.println("PCA9539: Device not found!");
#endif
        return false;
    }

    // ポート0を出力に設定（LED用）
    // 0x00 = すべて出力
    // 注意: 初期化中なので_initializedチェックをスキップ
    if (!writeRegister(REG_CONFIG_PORT0, 0x00)) {
        _error = true;
        _initialized = false;
        return false;
    }

    // ポート1を入力に設定（ボタン用）
    // 0xFF = すべて入力
    if (!writeRegister(REG_CONFIG_PORT1, 0xFF)) {
        _error = true;
        _initialized = false;
        return false;
    }

    // ポート0の極性反転を無効化（デフォルト）
    if (!writeRegister(REG_POLARITY_INV_PORT0, 0x00)) {
        _error = true;
        _initialized = false;
        return false;
    }

    // ポート1の極性反転を無効化（デフォルト）
    if (!writeRegister(REG_POLARITY_INV_PORT1, 0x00)) {
        _error = true;
        _initialized = false;
        return false;
    }

    // 初期状態を設定（すべてHIGH = LED消灯）
    _port0_state = 0xFF;
    if (!writeRegister(REG_OUTPUT_PORT0, _port0_state)) {
        _error = true;
        _initialized = false;
        return false;
    }

    // 割り込みピンの設定
    if (_int_pin >= 0) {
        pinMode(_int_pin, INPUT_PULLUP);
    }

    // ポート1の初期状態を読み取り
    // 注意: 初期化中なので_initializedチェックをスキップ
    uint8_t port1_value;
    Wire.beginTransmission(_i2c_addr);
    Wire.write((uint8_t)REG_INPUT_PORT1);
    uint8_t error = Wire.endTransmission(false);
    if (error != 0) {
        _error = true;
        _initialized = false;
        return false;
    }
    uint8_t bytesRead = Wire.requestFrom(_i2c_addr, (uint8_t)1, (uint8_t)true);
    if (bytesRead != 1) {
        _error = true;
        _initialized = false;
        return false;
    }
    _port1_state = Wire.read();

    // ボタン状態をキャッシュに保存
    _button1_state = !(_port1_state & (1 << PCA9539_PORT1_BUTTON1));
    _button2_state = !(_port1_state & (1 << PCA9539_PORT1_BUTTON2));

    _initialized = true;

#if DEBUG_ENABLED
    Serial.println("PCA9539: Initialized successfully");
#endif

    return true;
}

/**
 * @brief デバイスの存在確認
 */
bool PCA9539::isConnected() {
    Wire.beginTransmission(_i2c_addr);
    uint8_t error = Wire.endTransmission();
    return (error == 0);
}

/**
 * @brief ポート0（LED用）のピン状態を設定
 */
bool PCA9539::setLED(uint8_t pin, bool state) {
    if (pin > 7) {
        _error = true;
        return false;
    }

    // ビット操作でポート0の状態を更新
    if (state) {
        // LOW = LED点灯（ビットをクリア）
        _port0_state &= ~(1 << pin);
    } else {
        // HIGH = LED消灯（ビットをセット）
        _port0_state |= (1 << pin);
    }

    return writeRegister(REG_OUTPUT_PORT0, _port0_state);
}

/**
 * @brief LED1の状態を設定
 */
bool PCA9539::setLED1(bool state) {
    return setLED(PCA9539_PORT0_LED1, state);
}

/**
 * @brief LED2の状態を設定
 */
bool PCA9539::setLED2(bool state) {
    return setLED(PCA9539_PORT0_LED2, state);
}

/**
 * @brief LED3の状態を設定
 */
bool PCA9539::setLED3(bool state) {
    return setLED(PCA9539_PORT0_LED3, state);
}

/**
 * @brief ポート1（ボタン用）のピン状態を読み取り
 */
bool PCA9539::readButton(uint8_t pin) {
    // 初期化されていない場合はエラーを返す
    if (!_initialized) {
        return false;
    }
    
    if (pin > 7) {
        _error = true;
        return false;
    }

    uint8_t port1_value;
    if (!readRegister(REG_INPUT_PORT1, &port1_value)) {
        _error = true;
        return false;
    }

    _port1_state = port1_value;

    // ビットがLOW（0）の場合、ボタンが押されている
    return !(_port1_state & (1 << pin));
}

/**
 * @brief BUTTON1の状態を読み取り
 */
bool PCA9539::readButton1() {
    // 初期化されていない場合はエラーを返す
    if (!_initialized) {
        return false;
    }
    return readButton(PCA9539_PORT1_BUTTON1);
}

/**
 * @brief BUTTON2の状態を読み取り
 */
bool PCA9539::readButton2() {
    // 初期化されていない場合はエラーを返す
    if (!_initialized) {
        return false;
    }
    return readButton(PCA9539_PORT1_BUTTON2);
}

/**
 * @brief ボタン状態を更新（INTピンがLOWの時のみI2C通信を行う）
 */
bool PCA9539::updateButtonStates() {
    // 初期化されていない場合はスキップ
    if (!_initialized) {
        return false;
    }

    // INTピンが設定されている場合、INTピンがHIGHの時は更新不要
    if (_int_pin >= 0) {
        if (digitalRead(_int_pin) == HIGH) {
            // 割り込みなし（変更なし）
            return true;
        }
    }

    // INTピンがLOW、またはINTピンが未設定の場合はI2C通信で状態を更新
    uint8_t port1_value;
    if (!readRegister(REG_INPUT_PORT1, &port1_value)) {
        _error = true;
        return false;
    }

    _port1_state = port1_value;

    // ボタン状態をキャッシュに保存
    _button1_state = !(_port1_state & (1 << PCA9539_PORT1_BUTTON1));
    _button2_state = !(_port1_state & (1 << PCA9539_PORT1_BUTTON2));

    return true;
}

/**
 * @brief BUTTON1の状態を取得（キャッシュから）
 */
bool PCA9539::getButton1() const {
    return _button1_state;
}

/**
 * @brief BUTTON2の状態を取得（キャッシュから）
 */
bool PCA9539::getButton2() const {
    return _button2_state;
}

/**
 * @brief ポート0全体の状態を読み取り
 */
uint8_t PCA9539::readPort0() {
    uint8_t value;
    if (readRegister(REG_INPUT_PORT0, &value)) {
        _port0_state = value;
    }
    return _port0_state;
}

/**
 * @brief ポート1全体の状態を読み取り
 */
uint8_t PCA9539::readPort1() {
    uint8_t value;
    if (readRegister(REG_INPUT_PORT1, &value)) {
        _port1_state = value;
    }
#if DEBUG_ENABLED
        Serial.printf("PCA9539: Port1 (reg=0x%08X, )\n", value);
#endif
    return _port1_state;
}

/**
 * @brief ポート0全体の状態を設定
 */
bool PCA9539::writePort0(uint8_t value) {
    _port0_state = value;
    return writeRegister(REG_OUTPUT_PORT0, _port0_state);
}

/**
 * @brief ポート1全体の状態を設定
 */
bool PCA9539::writePort1(uint8_t value) {
    _port1_state = value;
    return writeRegister(REG_OUTPUT_PORT1, _port1_state);
}

/**
 * @brief 割り込みピンの状態を読み取り
 */
bool PCA9539::readInterrupt() {
    if (_int_pin < 0) {
        return false;
    }
    // INTピンはアクティブローなので、LOWの時に割り込み発生
    return (digitalRead(_int_pin) == LOW);
}

/**
 * @brief I2Cレジスタに書き込み
 */
bool PCA9539::writeRegister(Register reg, uint8_t value) {
    // 初期化中（REG_CONFIG_PORT0/1の設定時）は初期化チェックをスキップ
    // それ以外で初期化されていない場合はエラーを返す
    bool is_init_register = (reg == REG_CONFIG_PORT0 || reg == REG_CONFIG_PORT1 || 
                             reg == REG_POLARITY_INV_PORT0 || reg == REG_POLARITY_INV_PORT1 ||
                             reg == REG_OUTPUT_PORT0);
    if (!is_init_register && !_initialized) {
        return false;
    }
    
    Wire.beginTransmission(_i2c_addr);
    Wire.write((uint8_t)reg);
    Wire.write(value);
    uint8_t error = Wire.endTransmission();

    if (error != 0) {
        _error = true;
#if DEBUG_ENABLED
        Serial.printf("PCA9539: Write error (reg=0x%02X, error=%d)\n", reg, error);
#endif
        return false;
    }

    return true;
}

/**
 * @brief I2Cレジスタから読み取り
 */
bool PCA9539::readRegister(Register reg, uint8_t* value) {
    if (value == nullptr) {
        _error = true;
        return false;
    }

    // 初期化されていない場合はエラーを返す
    if (!_initialized) {
        _error = true;
        return false;
    }

    // I2C通信のタイムアウトを設定（100ms）
    Wire.setTimeout(100);
    
    Wire.beginTransmission(_i2c_addr);
    Wire.write((uint8_t)reg);
    uint8_t error = Wire.endTransmission(false); // false = リスタートを送信

    if (error != 0) {
        _error = true;
#if DEBUG_ENABLED
        Serial.printf("PCA9539: Read error (reg=0x%02X, error=%d)\n", reg, error);
#endif
        return false;
    }

    uint8_t bytesRead = Wire.requestFrom(_i2c_addr, (uint8_t)1, (uint8_t)true); // true = stop after request
    if (bytesRead != 1) {
        _error = true;
#if DEBUG_ENABLED
        Serial.printf("PCA9539: Read error (reg=0x%02X, bytesRead=%d)\n", reg, bytesRead);
#endif
        return false;
    }

    *value = Wire.read();
    return true;
}
