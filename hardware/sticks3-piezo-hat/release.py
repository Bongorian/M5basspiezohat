#!/usr/bin/python3
"""Publish only a fully connected, DRC-clean prototype PCB and PCBA files."""
from pathlib import Path
import csv,json,subprocess,shutil,zipfile,hashlib,datetime
import pcbnew as k
ROOT=Path(__file__).resolve().parent
name='sticks3-piezo-hat'
# Export the checked-in routed PCB; local routing candidates are never needed.
board=ROOT/(name+'.kicad_pcb')
def run(*args):subprocess.run(args,check=True)
run('kicad-cli','pcb','drc','--format','json','--exit-code-violations','-o',str(ROOT/'drc-final.json'),str(board))
run('kicad-cli','sch','erc','--format','json','--exit-code-violations','-o',str(ROOT/'erc.json'),str(ROOT/(name+'.kicad_sch')))
run('kicad-cli','sch','export','netlist','--format','kicadxml','-o',str(ROOT/'schematic.xml'),str(ROOT/(name+'.kicad_sch')))
(ROOT/'schematic.xml').write_text((ROOT/'schematic.xml').read_text().replace(str(ROOT)+'/', ''))
run('/usr/bin/python3',str(ROOT/'verify.py'),str(board))
b=k.LoadBoard(str(board));assert b.GetCopperLayerCount()==4;assert not [t for t in b.GetTracks() if not isinstance(t,k.PCB_VIA) and t.GetLayer()==k.In1_Cu and t.GetNetname()!='GND']
out=ROOT/'release';out.mkdir(exist_ok=True);gerbers=out/'gerbers';gerbers.mkdir(exist_ok=True);preview=ROOT/'preview'
run('kicad-cli','pcb','export','gerbers','--layers','F.Cu,In1.Cu,In2.Cu,B.Cu,F.Mask,B.Mask,F.Paste,F.Silkscreen,B.Silkscreen,Edge.Cuts','--use-drill-file-origin','-o',str(gerbers)+'/',str(board))
run('kicad-cli','pcb','export','drill','--format','excellon','--drill-origin','plot','--excellon-units','mm','--excellon-separate-th','--generate-report','--report-path',str(out/'drill-report.txt'),'-o',str(gerbers)+'/',str(board))
raw=out/'CPL_KiCad.csv';run('kicad-cli','pcb','export','pos','--side','front','--format','csv','--units','mm','--use-drill-file-origin','--smd-only','--exclude-fp-th','--exclude-dnp','-o',str(raw),str(board))
rows=list(csv.DictReader(raw.open()));print('POSITION COLUMNS',rows[0].keys())
refs=set()
for row in csv.DictReader((ROOT/'BOM_JLCPCB.csv').open()):refs.update(row['Designator'].split(','))
assert len(refs)==96;assert {r['Ref'] for r in rows}==refs
with (out/'CPL_JLCPCB.csv').open('w',newline='') as f:
 w=csv.writer(f);w.writerow(['Designator','Mid X','Mid Y','Layer','Rotation'])
 for r in rows:
  assert r['Side']=='top';w.writerow([r['Ref'],r['PosX'],r['PosY'],'Top',r['Rot']])
for fn in ['BOM_JLCPCB.csv','BOM_full.csv','HAND_ASSEMBLY.csv','drc-final.json','erc.json','verification.json']:shutil.copyfile(ROOT/fn,out/fn)
run('kicad-cli','pcb','export','svg','--layers','F.Cu,F.Fab,Edge.Cuts','--mode-single','--page-size-mode','2','--exclude-drawing-sheet','-o',str(preview/'routed-front.svg'),str(board))
run('kicad-cli','pcb','export','svg','--layers','In1.Cu,Edge.Cuts','--mode-single','--page-size-mode','2','--exclude-drawing-sheet','-o',str(preview/'ground-plane.svg'),str(board))
run('kicad-cli','pcb','export','glb','--include-tracks','--include-pads','--include-silkscreen','--include-soldermask','-o',str(preview/'routed-board.glb'),str(board))
readme='''# StickS3 Piezo Hat R1 — 試作基板の製造データ

2026-10-06。未接続0、DRC違反0、ERC違反0。回路図・基板・設計マニフェストの318接続を照合。PCBA 96点、すべて表面。裏面は裸のテスト銅箔4点。

## JLCPCBへ渡すファイル

- `Gerber_JLCPCB.zip`：基板製造用。銅箔4層、マスク表裏、表面ペースト、シルク表裏、外形、PTH/NPTHドリル、Gerberジョブ。
- `BOM_JLCPCB.csv`：LCSC部品番号付きの表面PCBA部品。
- `CPL_JLCPCB.csv`：同じ原点の配置データ。96個の部品名がBOMと完全一致。単位mm、Topのみ。
- `assembly-front.png`：部品位置を照合する図。QFNのピン1と全ICの回転を注文プレビューで照合する。
- `HAND_ASSEMBLY.csv`：D1–D5の低漏れ保護ダイオード、J6ヘッダ、J1–J5のピエゾ線はPCBA後に手実装。

## 基板条件

FR-4、4層、21×38mm、厚さ1.0mm、四隅R1mm、外層1oz、内層標準0.5oz、緑マスク、表面のみPCBA。内層1はGND専用、内層2は信号。外形から銅箔0.5mm以上。最小配線幅／間隔0.10／0.10mm、ビア外径0.40mm・穴0.20mm、通常の貫通ビア、表裏テント。SMDパッドへのドリル重なりは0、未充填ビアインパッドは使わない。マスク開口は銅箔パッドと1:1。

JLCPCBの現行能力表では、穴0.20mmで外径0.45mm未満のビアは追加料金対象。注文画面で対応する小径ビア設定を選ぶ。密度と外装48×24×15mmの条件を優先した寸法で、6層化はしていない。根拠：[JLCPCB能力表](https://jlcpcb.com/capabilities/pcb-capabilities)。部品実装可否・在庫・回転・パネル／治具条件・最終価格は見積プレビューで照合する。注文や支払いは行っていない。

## 接続と試作

ES7210×2のTDM入力をM5StickS3へ渡す基板。音程検出とUSB MIDIは本体のファームウェアが担う。Hat2の外部端子番号は基板READMEを参照。INT4は本体につながないためU4の13番ピンをNCとし、不要なプルアップR27を省いた。ADCの割り込みは初期設定で無効にする。

ヘッダC53207371の嵌合・向き、TDMカスケード設定、ピエゾ低域応答と遅延は試作で確認する。USB MIDIのデバイス接続にはUSBホストが必要。ケース寸法予算はコネクタ込み公称47.3×23.8×14.8mm。筐体のガイドで基板外周を保持するため基板固定穴はない。筐体モデルは機構検討用で、製造用STLではない。
'''
(out/'README.md').write_text(readme)
with zipfile.ZipFile(out/'Gerber_JLCPCB.zip','w',zipfile.ZIP_DEFLATED) as z:
 for f in sorted(gerbers.iterdir()):z.write(f,f.name)
v=json.loads((ROOT/'verification.json').read_text());status={'stage':'routing complete; fabrication and PCBA files generated','checked_at':datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=9))).isoformat(),'board':board.name,'layers':4,'board_mm':[21,38,1],'schematic_erc_violations':0,'pin_net_matches':v['pin_net_matches'],'unconnected_items':0,'other_drc_violations':0,'track_segments':v['track_segments'],'vias':v['vias'],'pcba_components':96,'unfilled_via_in_smd_paste':0,'non_ground_tracks_on_ground_plane':0,'fabrication_data_complete':True,'hardware_validated':False,'order_placed':False}
(ROOT/'routing-status.json').write_text(json.dumps(status,indent=2));(out/'release-status.json').write_text(json.dumps(status,indent=2))
print('RELEASE',status)
# Build upload bundles from the checked-in final source.
run('/usr/bin/python3',str(ROOT/'render_layout.py'))
with zipfile.ZipFile(out/'PCBA_JLCPCB.zip','w',zipfile.ZIP_DEFLATED) as z:
 for fn in ['Gerber_JLCPCB.zip','BOM_JLCPCB.csv','CPL_JLCPCB.csv','assembly-front.png','README.md','HAND_ASSEMBLY.csv','BOM_full.csv','drc-final.json','erc.json','verification.json','release-status.json','drill-report.txt']:z.write(out/fn,fn)
with zipfile.ZipFile(out/'KiCad_source.zip','w',zipfile.ZIP_DEFLATED) as z:
 for fn in [name+'.kicad_pcb',name+'.kicad_pro',name+'.kicad_sch','PiezoHat.kicad_sym','fp-lib-table','sym-lib-table','design.json','README.md','routing-status.json','KICAD_LIBRARIES_LICENSE.md']:z.write(ROOT/fn,fn)
 for f in (ROOT/'PiezoHat.pretty').iterdir():z.write(f,'PiezoHat.pretty/'+f.name)
files=['PCBA_JLCPCB.zip','Gerber_JLCPCB.zip','KiCad_source.zip','BOM_JLCPCB.csv','CPL_JLCPCB.csv','assembly-front.png']
(out/'manifest.json').write_text(json.dumps({n:{'bytes':(out/n).stat().st_size,'sha256':hashlib.sha256((out/n).read_bytes()).hexdigest()} for n in files},indent=2))
