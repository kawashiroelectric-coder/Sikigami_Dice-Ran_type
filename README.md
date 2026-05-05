# Sikigami_Dice-Ran_type
式神ダイスの橙タイプを作ったので設計を流用して藍タイプを作りました。
エンコーダ入力とボタン二つの入力でいろいろできます。
USBを繋がないとWifi関連の機能を使うことは出来ません

## 概要
- **マイコン:** ESP32C3
- **開発環境:** PlatformIO (VSCode)
- **主な機能:** 
  - 過去の記録
  - ゲーム（本当に簡単な物）
  - サイコロ
  - ポモドーロタイマー
  - 自然音の再生

  ## 回路図
ここに回路図の画像を貼る予定です。
![Rantype Main.jpg](https://github.com/kawashiroelectric-coder/Sikigami_Dice-Ran_type/blob/main/Rantype%20Main.jpg)

## 使い方
1. `platformio.ini` を確認してマイコンを接続
2. ビルドして書き込み
3. シリアルモニタで動作確認

## 備考
- 現在開発中のため、コードが整理されていない部分があります。
