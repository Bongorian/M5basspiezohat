# ファームウェア実装・評価手順

M5StickS3用ESP-IDFアプリケーションは `firmware/` に収録している。[導入・操作手順](../firmware/README.md)を参照。ESP32-S3ビルドと合成信号のホストテストは確認済み。ADCカスケード、USB接続、演奏遅延、CPU負荷の実機測定は未実施。

## 信号経路

```mermaid
flowchart LR
  P[独立ピエゾ 4〜5入力] --> A[保護・バッファ・LPF・AC結合]
  A --> D[ES7210 ×2]
  D -->|8スロット TDM| M[I²S受信・DMA]
  M --> Q[32フレームの取得キュー]
  Q --> F[DC除去・FIR・4kHz間引き]
  F --> Y[弦別YIN・包絡線・発音状態]
  Y --> R[MIDIチャンネル割り当て]
  R --> U[USB MIDIデバイス]
  U --> H[接続先USBホスト・外部音源]
```

## 電源・クロック・ADC

M5Unified初期化では内蔵音声・IMU・RTCを無効にする。本体がM5StickS3と認識された場合だけ `M5.Power.setExtOutput(true)` でHat2 EXT_5Vを供給する。GPIOは3.3V。出力中のEXT_5Vへ別電源を接続しない。[M5StickS3資料](https://docs.m5stack.com/en/core/StickS3)

| 信号 | GPIO / アドレス |
| --- | --- |
| SDA / SCL | GPIO4 / GPIO5、外部I²Cポート1、100kHz |
| MCLK / BCLK / WS / DATA | GPIO1 / GPIO7 / GPIO6 / GPIO8、I²Sポート0 |
| U3 / U4 | 7bit I²C 0x40 / 0x41 |
| USB | 本体USB-C、ESP32-S3のネイティブUSB |

U3 TDMOUT→33Ω→U4 TDMIN、U4 TDMOUT→33Ω→Hat2 DATA。内蔵PMICのGPIO47／48のI²Cポート0と外部ADCバスは分離している。

I²S受信を有効にしてクロックを供給した後、両ADCのID、リビジョンを読み取り、初期化する。設定の読み戻しと120ms後の状態確認を行い、取得を再開して起動直後の64ブロックを破棄する。ADC異常時は取得タスクを開始せず、表示・USB診断・固定ノート試験を維持する。

### 主なADC評価設定

完全な設定表は [es7210.cpp](../firmware/main/es7210.cpp)。以下はES7210 Rev.21資料を基にしたカスケード評価設定で、実機のリビジョンと音声フレームを照合する必要がある。

| レジスタ | 値 / 用途 |
| --- | --- |
| 0x02 / 0x03 / 0x04 / 0x05 / 0x07 | C1 / 02 / 01 / 00 / 20、16kHz・256fs |
| 0x08 | 44、8入力・スレーブ・EQバイパス |
| 0x11 | 63、16bit DSP A。Philips選択時60 |
| 0x12 | U3=07、U4=03、カスケード・最終段指定 |
| 0x0C / 0x10 / 0x16 | 00、割り込み・DMIC・ALCを無効化 |
| 0x20〜0x23 | 0D、低速HPFと自動オフセット補正の評価値 |
| 0x43〜0x46 | 10＋PGA設定、初期0dB |
| 0x4B / 0x4C | 40、ADC/PGA有効、未使用MICBIAS停止 |

[ES7210 Rev.21資料](https://files.waveshare.com/wiki/common/ES7210_DS.pdf)、[EspressifのTDM例](https://github.com/espressif/esp-idf/blob/v5.4.2/examples/peripherals/i2s/i2s_codec/i2s_es7210_tdm/main/i2s_es7210_record_example.c)を参照。設定レジスタが読み戻せることと、カスケード音声が正しく整列することは別に検証する。入力パッドへ既知の周波数を順に与え、全8スロットを観測してJ1〜J5への対応を確定する。

## 取得・タスク

| 項目 | 実装値 |
| --- | ---: |
| 取得レート | 16kS/s |
| フレーム | 8スロット×16bit |
| クロック計算値 | BCLK 2.048MHz、MCLK 4.096MHz |
| 取得ブロック | 32フレーム、512B、2ms |
| DMA | 8バッファ、4096B、16ms容量 |
| 取得→解析キュー | 16ブロック、約8kB、最大32ms分 |
| MIDIイベントキュー | 128イベント |
| USB / 取得 / 解析タスク | Core0優先度6 / Core0優先度22 / Core1優先度17 |
| 表示 | 主タスク、200ms周期 |

DMA容量16msはブロック待ち時間2msとは異なる。取得キューは処理時間の変動を吸収するが、遅延を一定に保証しない。DMAオーバーフロー、取得エラー、ブロック連番の欠損、送信不足ではPanicを要求し、音程履歴・ノート所有情報をクリアする。USBの再接続時も設定チャンネルにCC120／123とPitch Bend中央値を送り、必要時だけRPNで±2半音を設定する。

受信タスクは解析を行わず、ブロックのコピーと連番を管理する。音程処理は取得とは別Coreで行う。USBのFIFO不足は4byteのMIDIパケットを保持して再送し、Panic前の古いイベントは世代番号で捨てる。ISRのオーバーフロー数はFreeRTOSのクリティカルセクションで保護する。[ESP-IDF I²S仕様](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32s3/api-reference/peripherals/i2s.html)

USBはNVSに保存した役割を起動時に読み込む。デバイスモードはTinyUSBのMIDI＋CDC、ホストモードはESP-IDF USB Host Libraryと専用MIDI OUTクライアントを使用する。同じ内蔵PHYへ両方のドライバを同時に入れず、役割変更時はミュートして保存・再起動する。Aを押したままリセットするとその起動だけデバイスモードを強制する。

ホストのライブラリ処理はCore0優先度5の別タスク。全クライアントAPIと転送完了処理は既存のUSB送信タスクで実行する。Full-Speed MIDI 1.0のalternate setting 0、最初のbulk OUT、cable 0を選ぶ。非対応記述子・不正長・MIDI 2.0専用インターフェースは拒否する。非同期転送を1件ずつ処理し、1秒のソフトウェア監視で無応答を停止・キャンセルする。切断時は完了コールバックを待ってから転送・インターフェース・デバイスを解放する。

StickS3はUSB-CへVBUSを供給しないため、ホストでは外部の給電・逆流防止・Type-C役割条件を満たす接続が必要。ホスト時のCDCは提供しない。操作・制限は [USB切り替え手順](../firmware/README.md#usbホストデバイス切り替え) を参照。[USB MIDI 1.0仕様](https://www.usb.org/sites/default/files/midi10.pdf)、[ESP-IDFホストAPI](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32s3/api-reference/peripherals/usb_host.html)

## 音程・発音

入力別DC除去（係数0.9995）→63タップHamming FIR（カットオフ900Hz）→4分の1間引きで4kHzへ変換する。履歴は384サンプル、YIN解析の周期は32サンプル＝8ms。FIRの位相は受信ブロックの境界をまたいで保持する。

弦の開放音−1半音〜＋25半音を探索範囲とする。YIN確信度0.85以上、2回の安定判定とRMSゲートでNote Onを生成する。Note変更には0.65半音のヒステリシスを適用する。ゲート以下30ms、または確信度不足120msで停止する。RMSから対数スケールのVelocityを求める。同じノートの再ピッキングは包絡線の上昇比3と100msの間隔で検出する。判定パラメータは合成信号での評価値。

標準は共通MIDIチャンネル1。弦別ノート所有情報と参照数で同音のNote Offを管理する。任意の弦別チャンネル設定ではチャンネルを連続割り当てし、Pitch Bendを個別送信できる。受信音源も各チャンネルに対応する必要がある。

合成開放音の最初のNote Onまでのサンプル時間はB0=80ms、E1=64ms、A1=48ms、D2=48ms、G2=40ms。ADCフィルタ・USB・音源遅延と実機のCPU時間を含まない。

## 実機評価

| 段階 | 確認項目 |
| --- | --- |
| 電源・接続 | ヘッダ方向、EXT_5V、3V3A／3V3D、VMID、I²Cアドレス |
| USB | MIDIとCDCの認識、`test` による固定ノート発音、切断・再接続 |
| クロック・ADC | MCLK／BCLK／WS、8スロット、形式、実レート、入力順、再起動時の再現性 |
| 低域 | 前段・AC結合・ADC HPFを含む10〜100Hz応答、Low B=30.87Hzの減衰 |
| 入力範囲 | 最大アタックとクリップ、弱音とゲート、過負荷後の回復、無給電時の回り込み |
| 同期 | 同じ信号を両ADCへ与え、固定遅延とチャンネルずれを測定 |
| DMA・CPU | 10分以上の5入力取得、表示・USB同時使用、欠損、最大・平均処理時間、空きRAM |
| 演奏 | 遅延の中央値・95パーセンタイル、オクターブ誤り、余分なNote On、弦間漏れ、再ピッキング |

前段は20MΩ入力バイアス、TLV9064利得1、1kΩ／47nFの2段LPF、10µF AC結合。ピエゾ容量と負荷抵抗も低域応答に影響する。Cp=1nF、R=20MΩの簡略モデルではカットオフ約7.96Hzだが、ADC入力抵抗・実効容量・HPFを合わせて測定する。[TIピエゾ信号調整](https://www.ti.com/lit/an/sloa033a/sloa033a.pdf)

試験は標準の起動ミュートから開始し、全入力のスロット対応を確認してから発音を有効にする。ソフトウェアは電気的信号の音程を解析するため、ブリッジでの機械的な弦間漏れを自動分離しない。実機評価で設定・回路を変更した場合は、ビルド、テスト、基板検査、製造データを更新する。

分離の導入順序、校正録音、付属のオフライン学習ツールは [クロストーク分離・校正学習](crosstalk-separation.md) を参照。
