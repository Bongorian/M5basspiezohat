# M5basspiezohat

M5StickS3のHat2に接続する、エレキベース用の5チャンネル・ピエゾ入力基板です。弦ごとの信号を増幅・変換し、TDMでM5StickS3へ送り、本体で音程を検出してUSB MIDIで外部音源を鳴らす構成です。

```text
独立ピエゾ ×4〜5 → 保護・高入力抵抗バッファ・LPF
                 → ES7210 ×2 → I²S/TDM・DMA → M5StickS3 → USB MIDI
```

## 現在の状態

基板の配線と製造データの出力を完了しました。KiCad 9.0.9で未接続0件、DRC違反0件、ERC違反0件、回路図・基板・設計マニフェストの318接続を照合済みです。

| 項目 | R1試作基板 |
| --- | --- |
| 基板寸法 | 21×38×1.0mm、4層、四隅R1mm |
| 実装面 | 部品は表面のみ。裏面は裸銅箔のテストパッド4点 |
| PCBA | LCSC品番付き96点、全点Top |
| 手実装 | D1〜D5保護ダイオード、J6ヘッダ、ピエゾ配線 |
| 主な部品 | ES7210×2、TLV9064×2、AP2112K-3.3×2 |
| ケースの寸法目標 | Hat単体・コネクタ込み48×24×15mm以内 |

![最終基板の部品位置・ピン1](hardware/sticks3-piezo-hat/release/assembly-front.png)

## 設計・製造データ

- [回路・部品・Hat2接続・再生成手順](hardware/sticks3-piezo-hat/README.md)
- [KiCadプロジェクト](hardware/sticks3-piezo-hat/sticks3-piezo-hat.kicad_pro)／[回路図](hardware/sticks3-piezo-hat/sticks3-piezo-hat.kicad_sch)／[配線済み基板](hardware/sticks3-piezo-hat/sticks3-piezo-hat.kicad_pcb)
- [JLCPCB向け一式ZIP](hardware/sticks3-piezo-hat/release/PCBA_JLCPCB.zip)／[発注条件](hardware/sticks3-piezo-hat/release/README.md)
- [編集用KiCadソースZIP](hardware/sticks3-piezo-hat/release/KiCad_source.zip)
- [機構検討・Blenderモデル](mechanical/sticks3-hat-concept.md)
- [取得・音程検出・MIDIの計画](m5stack-piezo-midi-plan.md)

## 試作で確認する項目

ハードウェアは未製造・未実機検証です。Hat2ヘッダの嵌合と向き、ES7210のTDMカスケード設定、Low Bまでの低域応答、弦間の振動漏れ、音程確定までの遅延を確認します。M5StickS3の音程検出・USB MIDIファームウェアは計画段階で、このリポジトリには実装していません。

ケースは寸法・保持構造を検討するモデルです。蓋・下端受けの固定方法とケーブル出口は未設計で、製造用STLではありません。USB MIDIで外部音源を鳴らす場合は、接続先のUSBホスト機能、またはPC／USB MIDIホストが必要です。

4層・0.10mm配線／間隔・外径0.40mm／穴0.20mmビアを使用します。小径ビアの追加料金、PCBA部品の在庫と向きは発注画面で照合してください。発注・支払いは行っていません。

## 管理方針

`main`で現行設計を管理します。`hardware/sticks3-piezo-hat/release/`が製造データ、同階層のKiCadファイルが編集元です。途中の配線候補、キャッシュ、ダウンロードしたメーカー資料は公開対象から外しています。資料の出典は設計READMEから参照できます。

KiCad由来のライブラリとモデルの出典・ライセンスは [NOTICE.md](NOTICE.md) を参照してください。プロジェクト独自部分の利用ライセンスは未指定です。
