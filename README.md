# Pico2UltraHiResUSBDDC (USB Digital Audio Device)

本プロジェクトは、**USB Audio Class 1.0** に準拠した **USBデジタルオーディオコンバータ（USB-DDC）** です。  
USB経由で入力された2ch PCMオーディオ信号に対し、高品質なアップサンプリング処理を行い、**I²Sインターフェイス**を通じてDACチップへ出力します。

---

## 🔍 概要

本デバイスは、PCなどのUSBホストからオーディオ信号を受け取り、**リアルタイムで最大32倍のアップサンプリング**を行い、DACチップへ32bit I²S形式で出力するUSB-DDC（Digital to Digital Converter）です。  
信号処理には Raspberry Pi Pico2（RP2350）を採用し、**マルチコア構成・DMA・PIO** を駆使して低遅延・高精度なオーディオ信号伝送を実現しています。

---

## ✨ 特長

- USB Audio Class 1.0 準拠（OS標準ドライバで動作）
- リアルタイム・マルチステージ BiQuad IIR フィルタによる最大32倍アップサンプリング
- 2ch ステレオ PCM 入力（16bit / 24bit）
- 複数のDACチップと動作確認済（例：TI PCM5102, ESS ES9010K2M）

---

## 🔈 入力仕様（USB側）

| パラメータ       | 値                                |
| ---------------- | --------------------------------- |
| オーディオクラス | USB Audio Class 1.0               |
| チャンネル数     | 2ch ステレオ                      |
| ビット深度       | 16bit / 24bit (整数型)            |
| サンプルレート   | 44.1kHz / 48kHz / 88.2kHz / 96kHz |

---

## 🔊 出力仕様（I²S側）

| パラメータ     | 値                                                                              |
| -------------- | ------------------------------------------------------------------------------- |
| フォーマット   | I²S 32bit（左右チャンネル交互）                                                 |
| チャンネル数   | 2ch ステレオ                                                                    |
| ビット深度     | 固定 32bit（int）                                                               |
| サンプルレート | 最大 1536kHz / 1411.2kHz（入力に応じて自動切替）                                |
| 対応出力周波数 | 1536kHz / 1411.2kHz , 768kHz / 705.6kHz , 384kHz / 352.8kHz , 192kHz / 176.4kHz |

---

## 🎧 対応DACチップ

- **TI PCM5102**
- **ESS ES9010K2M**  
その他、32bit I²S インターフェースをサポートする DAC チップで使用可能です。

---

## ⚙ 使用技術・構成

- **RP2350**（Raspberry Pi Pico2）
- **DMA + PIO による I²S 出力**
- **マルチコア処理**  
  - Core0：USB通信処理 + アップサンプリング処理  
  - Core1：アップサンプリング処理 + DMA + I²S 送信処理
- **アップサンプリング構成**  
  - Core0:3段構成の BiQuad-IIR による 2x × 2x × 2x = 8x 拡張
  - Core1:1段構成の BiQuad-IIR による 4x 拡張
- **USB制御**  
  - LUFAベースの USB Audio Class 実装
- **タイミング制御**  
  - timer 割り込み + バッファレートに応じたフィードバック制御

---

## 🔧 ビルド・使用方法

1. VisualStudioCode上でRaspbarryPiPico拡張機能をインストール・有効化してください
2. 本リポジトリをクローンしてください
3. VisualStudioCodeにてCompileを行ってください (コンパイルしたファイルはbuild/srcディレクトリに生成されます)
4. RaspberryPiPico2のBOOTSELボタンを押しながらRaspberryPiPico2をPCに接続してください
5. 書き込みが完了すると自動的にOS標準のUSBオーディオデバイスとして認識されます 

---

## 🔧 コンフィグレーション

RP2350(RaspberryPiPico2)上の仕様で可能な範囲で、出力ピンアサインおよびアップサンプリング設定を任意に変更できます。
変更する場合は、 src/common.h にある"User Configurable"項を編集してください  
　　
### ピンアサイン

デフォルトでは以下のようになっています

I2S
- I2S DATA : GP20
- I2S BCLK : GP21
- I2S LRCK : GP22

I2C (PCM5102では不要)
- SDA : GP26
- SCL : GP27

DAC ENABLE(PCM5102では不要)
- DAC ENABLE PIN : GP28

パワーモード切替(オプション)
- POWERMODE SW PIN : GP18

### アップサンプリング設定

デフォルトでは 384kHz/352.8kHz になっています。

| 出力サンプルレート | ALWAYS_HIGH_POWER | ALWAYS_LOW_POWER | BYPASS_CORE1_UPSAMPLING | CORE0_UPSAMPLING_192K |
| ------------------ | ----------------- | ---------------- | ----------------------- | --------------------- |
| 1536kHz/1411.2kHz  | true              | false            | false                   | false                 |
| 768kHz/705.6kHz    | false             | true             | false                   | false                 |
| 384kHz/352.8kHz    | *                 | *                | true                    | false                 |
| 192kHz/176.4kHz    | *                 | *                | *                       | true                  |

---

### ESS DAC Specific

ESS製DAC(ES9010K2M)にて使用する場合は、USE_ESS_DAC をtrueにしてください。

## 📚 ライセンス

MIT License  
Copyright 2025 ArqAlice

---

## 📝 参考文献
- [Interface ラズパイPico DAC特設ページ](https://interface.cqpub.co.jp/pico_dac/)
- [USB Audio Class 1.0 Spec (USB.org)](https://www.usb.org/documents)