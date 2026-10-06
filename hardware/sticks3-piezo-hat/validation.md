# R1設計検査

対象：`sticks3-piezo-hat.kicad_pcb`／`sticks3-piezo-hat.kicad_sch`。KiCad 9.0.9。実行日時は各JSONレポートに記載。

| 検査 | 結果 | 記録 |
| --- | ---: | --- |
| PCB未接続 | 0件 | [drc-final.json](drc-final.json) |
| PCB DRC違反・警告 | 0件 | [drc-final.json](drc-final.json) |
| 回路図ERC違反・警告 | 0件 | [erc.json](erc.json) |
| 回路図・PCB・マニフェストの接続一致 | 318接続 | [verification.json](verification.json) |
| SMDパッドとビア穴の重なり | 0件 | [verification.json](verification.json) |
| GND専用内層の信号配線 | 0件 | `release.py`による検査 |
| PCBA BOMとCPLの参照名・実装面 | 101点一致、Topのみ | `release.py`による検査 |

CPLはU3・U4・U5・U6に270°の品番別補正を適用し、D1〜D5をPCBAへ追加。補正一覧と基板上のピン1座標を`release/rotation-audit.csv`に記録する。新しいJLCPCB配置プレビュー、部品照合・調達可否、DFMは未確認。

配線は2410セグメント、161ビア。実部品は表面のみ。裏面はTP1〜TP4の裸銅箔。

`release.py`はDRC／ERC、接続照合、実装面・ビア穴・内層配線の検査を実行し、合格後に製造データを出力する。幾何・接続の検査結果であり、アナログ特性、Hat2嵌合、ADC取得動作、MIDI出力の実機検証は含まない。
