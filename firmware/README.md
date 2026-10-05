# M5StickS3 firmware

M5basspiezohat R1の5入力をES7210×2からTDM/DMAで取得し、弦別の音程をUSB MIDI 1.0へ変換するESP-IDFアプリケーション。M5StickS3専用。実機でのADCカスケード取得・音源発音・処理速度は未検証。

## 実装内容

- 16kHz、8スロット、16bitのTDM受信。32フレーム単位、DMA 8バッファ。
- ES7210のI²C初期化、チップID・設定・動作状態の読み戻し。
- 4／5弦の独立解析。DC除去、63タップFIR、4kHzへの間引き、YIN音程推定。
- Note On／Off、入力強度に応じたVelocity、再ピッキング。弦別チャンネル時は任意で±2半音Pitch Bend。
- USB MIDIとUSB CDC診断の複合デバイス。液晶表示、ボタン操作、入力スロット変更。
- USB MIDIホスト送信。モードをNVSへ保存し、再起動でデバイス／ホストを切り替える。
- USB再接続、DMA欠損、送信キュー不足時のAll Sound Offと解析履歴のクリア。
- 発音開始は短い観測＋4ms更新、持続音は長い観測＋8ms更新。弦ごとの判定時刻を分散し、USBホストでは複数イベントをまとめて送信する。

## 標準設定

| 項目 | 値 |
| --- | --- |
| 本体 | M5StickS3、ESP32-S3、Flash 8MB／OPI PSRAM 8MB |
| ピエゾ入力 | J1〜J5 = B0／E1／A1／D2／G2、MIDIノート23／28／33／38／43 |
| TDM | DSP A、1 BCLK幅WS、1 bit遅延、8×16bit |
| クロック計算値 | MCLK 4.096MHz、BCLK 2.048MHz、WS 16kHz |
| 入力スロット | J1〜J5 = 0〜4。実基板で確認・修正が必要 |
| ADC PGA | 0dB |
| 発音／停止ゲート | 正規化RMS 0.004／0.002 |
| MIDI | 全弦をチャンネル1へ送信、Pitch Bendなし |
| 起動 | ミュート。確認後に `arm` またはAボタンで発音開始 |
| 音程追従 | 高速開始設定を有効。持続音は長い観測へ戻す |

USB-Cは標準でMIDI**デバイス**として動作する。PC、USB MIDIホスト、USBホスト端子のある音源へ接続する。通常のUSBデバイス端子だけを持つ音源へ接続する場合は、後述のホストモードと外部VBUS給電を使用できる。DIN MIDI出力は搭載していない。デバイスモードのUSB VID/PIDは開発用設定 `303A:4001`。

## USBホスト／デバイス切り替え

`role host` またはBボタン2秒長押しでミュートし、設定保存後に再起動する。B長押しは現在の役割を反転する。次回起動でも保存したモードを使う。デバイスモードでは `role device` も指定できる。モード切り替えはホットスワップではない。

**StickS3本体はUSB-CへVBUSを供給しない。** 本体回路図のUSB電源は入力経路で、固定したM5Unified v0.2.25の `setUsbOutput` はStickS3では動作しない。ホストモードには、音源のUSB端子へ5Vを供給し、StickS3側への逆流を防止できる外部給電アダプタなどの適切な接続回路が必要。電源をつないだだけのYケーブルや、動作が不明なハブは前提にしない。本体と音源の電源条件、Type-Cの役割・CC条件も接続機器に合わせて確認する。ソフトウェアのホスト切り替えだけでバス給電型音源へ直結できることを意味しない。[本体資料・回路図](https://docs.m5stack.com/en/core/StickS3#schematics)

公開回路図ではCC1／CC2に5.1kΩの固定プルダウン（R3／R4）がある。ファームウェアはこの抵抗を切り替えられず、USB-CのDRP／電力役割の切り替えは実装しない。ホストPHYで通信できることと、Type-Cポートが適切なホスト接続を構成できることを分けて評価する。外部接続回路はVBUSだけでなくCC条件も満たす必要があり、一般的なUSB-Cケーブルでの音源直結は保証しない。

ホストはFull-Speedのクラス準拠USB MIDI 1.0、最初の設定のalternate setting 0、最初の対応するbulk OUT、cable 0へ送信する。複合デバイス内のMIDIインターフェースにも対応する。MIDI 2.0専用、独自ドライバ必須、複数音源・ハブ、多ケーブル選択、MIDI受信は対象外。MIDI端子のあるUSB音源・USB MIDIインターフェースを外部デバイスとして接続する。[ESP-IDFホスト仕様](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32s3/api-reference/peripherals/usb_host.html)

ホストモード中は同じUSB-CでPCへCDC診断を提供できない。LCDに `HOST` と接続状態を表示する。Aクリックで発音切替、Aを1秒長押しで固定ノート試験、BクリックでPanic、Bを2秒長押しでデバイスモードへ戻る。USBは対応音源の列挙・初期Panic完了後に `ready` となる。

復旧時はAを押したままリセットすると、その起動を強制デバイスモードにできる。保存モードは変更しないため、CDCから `role device` で保存する。書き込みが必要なら本体のROMダウンロードモードを使う。PCへの接続前にデバイスモードへ戻す。

切断・再接続で解析状態をクリアする。転送異常や1秒以上応答しない音源では送信を停止し、再接続を要求する。ケーブル断や応答不能で届かないNote Offはソフトウェアで保証できないため、音源側の停止操作も利用する。ホストの給電と実機の列挙・発音は未検証。

## ビルド

LinuxでGit、Python 3、CMake、NinjaとESP-IDFの[インストール前提パッケージ](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32s3/get-started/linux-macos-setup.html)を用意する。リポジトリのルートから実行する。

```sh
bash firmware/tools/setup.sh
bash firmware/tools/build.sh
```

既にESP-IDF v5.4.2がある場合は、その `export.sh` を読み込んでから `build.sh` を実行できる。ローカルの `firmware/.tools/esp-idf` がある場合はそちらが優先される。setupはSDKのコミットを検査する。ライブラリの版と取得ハッシュは `main/idf_component.yml` と `dependencies.lock` に固定している。

```sh
bash firmware/tools/build.sh menuconfig
```

`M5basspiezohat` メニューで弦数、スロット、ゲート、PGA、MIDIチャンネル、弦別チャンネル、Pitch Bend、起動ミュート、TDM形式を設定する。4弦設定はJ1〜J4=E1／A1／D2／G2。弦別チャンネルは基本チャンネルから連続して使用し、最後が16を超える設定では発音しない。スロット重複、停止ゲート以上に低い発音ゲートも無効設定となる。

`Short startup observation and 4 ms initial pitch update`（`CONFIG_BASS_FAST_TRACKING`）は標準で有効。無効にすると発音開始から長い観測＋8ms更新を使う。どちらも確信度0.85と2回の安定判定を維持する。計算処理の最適化は両設定で有効。比較値と実機での確認条件は [処理時間と発音遅延](performance.md) を参照。

TDM形式のPhilipsはカスケード評価用の別設定。スロットの並びも変わり得るため、形式を変更したら全入力を再確認する。任意のチューニングは `main/board.hpp` の `open_notes` を変更する。

## 書き込み

書き込みは既存の本体ファームウェアとパーティションテーブルを置き換える。M5StickS3をUSBでPCへ接続し、OSが割り当てたポートに置き換える。

```sh
bash firmware/tools/build.sh -p /dev/ttyACM0 flash
```

接続できない場合は本体のダウンロードモードへ入れて再試行する。起動後は複合USBデバイスへ切り替わるため、CDCポート番号が変わる場合がある。[本体の公式資料](https://docs.m5stack.com/en/core/StickS3)も参照。

`release/m5basspiezohat-sticks3-0.2.0.zip` は高速開始設定を含む標準設定でのビルド済みファームウェア。0.1.0のZIPも比較用に保持する。展開先でPython環境に `esptool==4.9.0` を導入し、次のように書き込める。ブートローダ、パーティション、アプリをそれぞれ指定し、NVS領域の一括消去は行わない。

```sh
python -m esptool --chip esp32s3 --port /dev/ttyACM0 --baud 460800 write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x0 bootloader/bootloader.bin \
  0x8000 partition_table/partition-table.bin \
  0x10000 m5basspiezohat.bin
```

ビルド済みZIPの `BUILD_INFO.json` にSDK・設定・SHA-256を記録している。自前ビルドのパッケージ化は `python3 firmware/tools/package.py`。

## 初回確認

1. 電源を切ってHat2とピエゾを接続する。[接続表](../hardware/sticks3-piezo-hat/README.md#hat2接続)に従う。
2. USBで起動し、LCDの `MUTED` とADC状態を確認する。ADC異常時もUSB診断と固定ノート試験は使用できる。
3. USB MIDIホスト側で `M5basspiezohat R1` を選び、音源をMIDIチャンネル1に設定する。
4. CDC端末から `test` を送る。ノート36、Velocity 80を300ms発音し、ピエゾからの発音はミュートする。
5. `slots` を観測し、J1〜J5へ順番に既知の信号を入力する。各入力に対応する0〜7のスロットを特定する。ピエゾ弦間の機械的漏れがある場合は、入力パッドへの電気信号で確認する。
6. 例えば対応が6／3／0／7／2なら `map 6 3 0 7 2`。続けて `status` で反映を確認する。`map` はミュートする。変更はRAM上のみなので、常用値はmenuconfigへ保存して再ビルドする。
7. `arm` またはAボタンで発音を開始する。無音でNote Off、各弦の開放音、同時発音を確認する。Aでミュート切替、BでミュートとPanic。

標準の共通チャンネルでは、複数弦の同じノートを1音として保持し、最後の弦が停止したときにNote Offを送る。その間の同じ音の再アタックは統合される。弦ごとの同音再発音やPitch Bendが必要なら、弦別チャンネルを有効にし、音源の受信チャンネルを揃える。Pitch Bend時はRPNで±2半音を設定する。

## 診断

CDCは115200設定の改行区切りASCIIコマンド。USB MIDIと同時に利用できる。Python補助ツールは `python3 -m pip install -r firmware/tools/requirements.txt` で依存を導入する。

```sh
python3 firmware/tools/console.py /dev/ttyACM0 status
python3 firmware/tools/console.py /dev/ttyACM0 slots --watch
python3 firmware/tools/console.py /dev/ttyACM0 map 6 3 0 7 2
python3 firmware/tools/console.py /dev/ttyACM0 arm
```

| コマンド | 内容 |
| --- | --- |
| `help` | コマンド一覧 |
| `status` | ADC／USB状態、ミュート、欠損、最大DSP処理時間、空きRAM、弦別音程・確信度・クリップ数 |
| `slots` | 全8スロットのRMSと直近ブロックのピーク |
| `adc` | 両ADCの主要レジスタ読み戻し |
| `map S1 S2 S3 S4 [S5]` | J1から順にスロットを指定。重複不可 |
| `arm` | ADCと設定が正常なら発音開始 |
| `mute`／`panic` | 発音停止とAll Sound Off／All Notes Off |
| `test` | 設定した基本チャンネルで固定ノート36を発音。A長押しでも実行 |
| `role [host\|device]` | USB役割の確認／NVS保存と再起動 |

`dma_ovf`、`queue_drop`、`read_err`、`midi_drop`、`gaps` が増加しないことを確認する。`queue_peak` は取得キューからブロックを取り出した直後の最大待ちブロック数で、1ブロックは2ms。最大処理時間だけで平均負荷は判断しない。`dsp_us`／`max_dsp_us` は音程処理とスロット診断の経過時間を測り、タスクの割り込みを含み得る。共有状態の更新・USB・LCDの時間は含まない。`dsp_us` の増分を観測時間と比較し、10分以上の連続取得で欠損を調べる。状態の公開は16msごと。`clips` が増える場合はPGAを下げ、入力振幅と前段回路を確認する。

## 検証と制約

ホストテストは合成信号を使用し、Low B〜G4、倍音優勢波形、5入力同時検出、無音、DC、ノイズ、クリップ、任意サイズの受信ブロック、Velocity、Pitch Bend、再ピッキング、スロット変更、送信失敗、同音の共有とチャンネル割り当てを確認する。

```sh
cmake -S firmware/tests -B firmware/build-host -G Ninja
cmake --build firmware/build-host
ctest --test-dir firmware/build-host --output-on-failure
```

標準でAddressSanitizer／UndefinedBehaviorSanitizerを有効にする。GitHub ActionsでもホストテストとESP32-S3ビルドを実行する。

標準設定の合成開放音では、入力開始から最初のNote OnまでB0=64ms、E1=48ms、A1=40ms、D2=32ms、G2=28ms分のサンプルを使用した。これは各弦を単独の1入力設定で評価した結果で、5入力時は弦ごとに判定位相が異なる。ADC・USB・音源遅延と実機のCPU処理時間を含まない。演奏時の遅延保証ではない。

実機ではADCのリビジョン、8スロットの出力順、クロック、低域HPF、USBの両役割、CPU余裕、弦間漏れを確認する必要がある。レジスタの読み戻しが成功しても音声フレームの正しさは保証されない。USB役割だけをNVSへ保存し、スロット・ゲート・ゲインはビルド設定で管理する。OTAは実装していない。

弦間クロストークは [分離・校正学習の検討](../docs/crosstalk-separation.md) を参照。同期CSVから固定混合行列を学習するオフライン評価ツールと合成テストを収録している。リアルタイム分離・本体での学習・校正波形のUSB出力は未実装。

詳細は [実装設計・評価手順](../docs/firmware-design.md) と [依存ライブラリの出典](../NOTICE.md) を参照。
