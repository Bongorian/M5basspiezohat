# M5basspiezohat R1 — 基板仕様

弦ごとに独立したピエゾを最大5入力受け、ES7210×2のTDM出力をM5StickS3へ渡す。音程検出・USB MIDIは本体のファームウェアで処理する設計。基板は実機未検証、ファームウェアは未実装。

**筐体の設計範囲：Hat単体・コネクタ込み48×24×15mm以内。** 基板は21×38×1.0mm、四隅R1mm、4層。ケース本体41.1×23.8×14.8mm。候補ヘッダC53207371のメーカー図面に基づくコネクタ込み公称長47.3mm、公差予算込み47.9mm。実機嵌合・造形精度は未確認で、寸法保証ではない。基板に固定穴・切り欠きはなく、筐体の左右ガイド溝と上下の受けで支える機構案。受けと蓋の留め方は未設計。[機構案](../../mechanical/sticks3-hat-concept.md)を参照。

## 設計検査

KiCad 9.0.9で未接続0件、DRC違反0件、回路図ERC違反0件。配線済み基板は [sticks3-piezo-hat.kicad_pcb](sticks3-piezo-hat.kicad_pcb)。2410配線セグメント／161ビア、回路図／基板／設計マニフェストの318接続が一致。部品本体は表面のみ、裏面はTP1〜4の裸銅箔。内層GND面の信号線は0、SMDパッドとビア穴の重なりは0。

[JLCPCB用データ一式](release/README.md)：[Gerber ZIP](release/Gerber_JLCPCB.zip)、[PCBA BOM](release/BOM_JLCPCB.csv)、[CPL](release/CPL_JLCPCB.csv)、[組立位置図](release/assembly-front.png)。BOMとCPLは96個の部品名が完全一致し、全点Top。D1〜D5、J6、ピエゾ線は手実装。U4の割り込み出力（13番ピン）はNC。ADC初期設定では割り込みを無効にする。

最小配線幅／間隔0.10／0.10mm、貫通ビア外径0.40mm・穴0.20mm、表裏テント、マスク開口1:1。[JLCPCBの能力表](https://jlcpcb.com/capabilities/pcb-capabilities)に対応するが、外径0.45mm未満・穴0.20mmのビアは追加料金対象なので、注文時に小径ビアの条件を指定する。製造条件と手実装の手順は [manufacturing.md](manufacturing.md)、検査記録は [validation.md](validation.md) を参照。

## 回路

| 回路 | 仕様 |
| --- | --- |
| 入力 | SIG/GND ×5組、内部はんだパッド、穴0.8mm・径1.8mm |
| 保護 | 47kΩ直列＋低漏れBAV199Wの2ダイオードクランプ |
| 入力負荷 | 10MΩを2個直列にした20MΩでVMIDへバイアス |
| バッファ | TLV9064IRTER ×2、3×3mm WQFN、利得1 |
| 中点 | 47kΩ分圧＋1µF→U2Bバッファ、約1.65V |
| LPF | 各入力1kΩ／47nFを2段、受動回路 |
| ADC結合 | P側10µF直列、N側10µFでAC接地。ADCのDCバイアスを分離 |
| ADC | ES7210 ×2、入力8スロット、うち5スロットを音程解析 |
| 電源 | EXT_5V→AP2112K-3.3 ×2、3V3A／3V3D |
| クロック | MCLK/BCLK/WSに33Ω、データ／カスケードにも33Ω |

受動LPFは2段の間にバッファがないため、独立したRCを2個掛けた特性と異なる。ADC入力負荷を無視した等しいR/Cの伝達は `1/(1+3sRC+(sRC)^2)`、合成-3dB周波数は約1.27kHz。R=1kΩ、C=47nFによる計算値で、ADC入力インピーダンスを含む実際の応答は測定する。アタックと倍音を残せるか、ADC内PGA・HPFと合わせて確認する。

Low B・30.87Hzを保つため、ピエゾ容量、20MΩ負荷、AC結合容量、ADC内部HPFをまとめて評価する。低漏れ保護の代わりに汎用BAV99や漏れの大きいSchottkyへ無確認で置き換えない。高抵抗入力付近は組み立て後にフラックスを清掃する。無給電時のピエゾ電圧による電源への回り込みも試験する。

## LCSC部品と実装分担

| 部品 | LCSC | 個数 | 分担 |
| --- | --- | ---: | --- |
| ES7210・4入力ADC | [C365743](https://www.lcsc.com/product-detail/C365743.html) | 2 | PCBA |
| TLV9064IRTER・3×3mm WQFN | [C882406](https://www.lcsc.com/product-detail/C882406.html) | 2 | PCBA |
| AP2112K-3.3TRG1 | [C51118](https://www.lcsc.com/product-detail/C51118.html) | 2 | PCBA |
| BAV199W・CBI・SOT-323 | [C51315120](https://www.lcsc.com/product-detail/C51315120.html) | 5 | 手はんだ |
| 10MΩ・SAE・0402 | [C54531017](https://www.lcsc.com/product-detail/C54531017.html) | 10 | PCBA |
| 1kΩ・0402 | [C11702](https://www.lcsc.com/product-detail/C11702.html) | 12 | PCBA |
| 33Ω・0402 | [C25105](https://www.lcsc.com/product-detail/C25105.html) | 5 | PCBA |
| 47kΩ・0603 | [C25819](https://www.lcsc.com/product-detail/C25819.html) | 8 | PCBA |
| 100nF・0402 | [C1525](https://www.lcsc.com/product-detail/C1525.html) | 6 | PCBA |
| 47nF・0402 | [C82219](https://www.lcsc.com/product-detail/C82219.html) | 10 | PCBA |
| 1µF・0402 | [C52923](https://www.lcsc.com/product-detail/C52923.html) | 25 | PCBA |
| 10µF・0603 | [C19702](https://www.lcsc.com/product-detail/C19702.html) | 14 | PCBA |
| 2×8・2.54mm L型ヘッダ・BXCONN | [C53207371](https://www.lcsc.com/product-detail/C53207371.html)・候補、実機嵌合未確認 | 1 | 手はんだ |
| ピエゾ線 | 基板内パッドへ接続 | 5組 | 手はんだ |

部品番号・数量・実装分担は [BOM_full.csv](BOM_full.csv) を基準とする。PCBAには [BOM_JLCPCB.csv](BOM_JLCPCB.csv)、手実装には [HAND_ASSEMBLY.csv](HAND_ASSEMBLY.csv) を使用する。LCSCの販売在庫とJLCPCBの実装用在庫・部品区分・調達価格は個別に確認する。

## Hat2接続

J6番号は **M5の外部Hat2端子表の番号**。M5内部回路図のCN2番号とは異なる。

| J6 | M5信号 | 本基板 |
| ---: | --- | --- |
| 1 | GND | GND |
| 2 | G5 | SCL |
| 3 | EXT_5V | 5V_HAT |
| 4 | G4 | SDA |
| 6 | G6 | WS |
| 7 | G1 | MCLK |
| 8 | G7 | BCLK |
| 9 | G8 | TDM DATA |
| その他 | BOOT/BAT/3V3/5VIN等 | 未接続 |

この番号とGPIOは [M5公式端子表](https://docs.m5stack.com/en/core/StickS3) に基づく。標準ヘッダのピン1マークと本体のGND位置は、選定した部品の視方向と実物で照合する。嵌合と挿入長は実機で確認する。

`M5.begin()`後、`M5.Power.setExtOutput(true)`でEXT_5Vを出力にする。本体GPIOは3.3V。EXT_5Vを出力にした状態で、その端子へ別電源を接続しない。

ADCの7bit I²CアドレスはU3＝0x40、U4＝0x41を想定したAD0/AD1接続。アドレスは実機で確認する。U3 TDMOUT→33Ω→U4 TDMIN、U4 TDMOUT→33Ω→J6 DATA。共通MCLK/BCLK/WSを使う。レジスタ設定は実機試験で確定する。

## 取得・MIDIの初期条件

16kS/s、8スロット×16bit、MCLK4.096MHz、BCLK2.048MHzを評価開始点とする。DMAは32フレーム×8本、合計4096B／16ms容量。2msごとに取得し、解析側へ渡す。詳細は [ファームウェア設計](../../docs/firmware-design.md) を参照。

Low Bの周期は約32.4ms。DMA待ちを短くしても、音程確定に必要な観測時間は残る。音声取得、音程解析、USB送信、音源発音の遅延を分けて測定する。USB MIDIは本体USB-Cを使い、音源のUSBホスト機能またはPC／USB MIDIホストを経由する。DIN出力回路はこのHatには載せていない。

## ソースと再生成

- `sticks3-piezo-hat.kicad_sch`：全ピンを明示した回路図
- `sticks3-piezo-hat.kicad_pcb`：配線済み最終基板
- `sticks3-piezo-hat-placement.kicad_pcb`：再生成用の配置版
- `PiezoHat.pretty/`、`PiezoHat.kicad_sym`：プロジェクト内ライブラリ
- `design.json`：ピン、型番、部品座標
- `BOM_JLCPCB.csv`：PCBA部品のみ、標準4列
- `BOM_full.csv`、`HAND_ASSEMBLY.csv`：全調達／手実装
- `verify.py`、`verification.json`：接続と実装面の確認
- `preview/routed-front.png`、`preview/routed-board.glb`：最終基板の配線図・3D
- `release/`：Gerber、BOM、CPL、組立位置図、検査記録
- [ケースモデル](../../mechanical/sticks3-hat-concept.md)

KiCad 9.0.9の`kicad-cli`と`pcbnew`を読み込めるPythonが必要。Ubuntuでは`/usr/bin/python3`を使う。生成にはNumPy、図の出力にはPillow、補助ルータにはSciPyとC++17コンパイラを用意する。自動配線には別途`freerouting-cli`が必要。

```sh
cd hardware/sticks3-piezo-hat
/usr/bin/python3 generate.py
kicad-cli sch export netlist --format kicadxml -o schematic.xml sticks3-piezo-hat.kicad_sch
/usr/bin/python3 verify.py sticks3-piezo-hat-placement.kicad_pcb
/usr/bin/python3 route_compact.py sticks3-piezo-hat-placement.kicad_pcb NEW-routed.kicad_pcb
```

再生成は配置・回路図・BOMを上書きし、配線済み基板は別ファイルに保存する。KiCadのDSN出力へは0.127mm標準配線／0.10mm間隔、ビア径0.40mm・穴0.20mmを明示的に反映する。内層1はGND面、内層2は信号。QFN露出パッドの未充填ビアを禁止する。

## 組立・評価

[製造・組立手順](manufacturing.md)に従い、手実装部品とヘッダ方向を確認する。評価では電源・中点電圧、I²Cアドレス、ADC1個のTDM取得、2個のカスケード取得の順に確認する。ピエゾ接続後、Low Bの周波数応答、強いアタックでのクリップ、弦間漏れを測定する。

## 参照資料

メーカー資料：[ES7210レジスタ資料](https://files.waveshare.com/wiki/common/ES7210_DS.pdf)、[TLV9064](https://www.ti.com/lit/ds/symlink/tlv9064.pdf)、[CBI BAV199W](https://datasheet.lcsc.com/datasheet/pdf/08d81ae9ee2012e6b42c6110d851602f.pdf?productCode=C51315120)、[Espressif I²S/TDM/DMA](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/i2s.html)。

## 製造データの再出力

リポジトリルートから実行する。配線済み基板を入力に、DRC／ERC・接続照合後、Gerber・BOM・CPL・ZIP・チェックサムを再出力する：

```sh
cd hardware/sticks3-piezo-hat
/usr/bin/python3 release.py
```

## 補助ルータ

`finish_route.py`用の補助ルータは `hardware/sticks3-piezo-hat/` で次のように構築する。生成・配線ツールは検討用で、毎回DRC／ERCと接続照合を行う。

```sh
mkdir -p .build
g++ -O3 -std=c++17 finish_astar.cpp -o .build/piezo-finish-astar
```

プロジェクト内のKiCad由来ライブラリについては [ライセンス文書](KICAD_LIBRARIES_LICENSE.md) と [出典](../../NOTICE.md) を参照。
