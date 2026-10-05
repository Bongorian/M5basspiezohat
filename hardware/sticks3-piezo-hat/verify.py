#!/usr/bin/python3
"""Check pin-level parity, assembly side and unfilled via-in-pad risk."""
from pathlib import Path
import argparse,json,math,xml.etree.ElementTree as E
import pcbnew as k
p=argparse.ArgumentParser();p.add_argument('board',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parent;data=json.loads((root/'design.json').read_text());b=k.LoadBoard(str(a.board))
expected={(c['ref'],pn):net for c in data['components'] for pn,net in c['pins'].items() if net is not None}
actual={(f.GetReference(),pad.GetNumber()):pad.GetNetname() for f in b.GetFootprints() for pad in f.Pads() if pad.GetNetname()}
schematic={}
for n in E.parse(root/'schematic.xml').getroot().findall('./nets/net'):
 for node in n.findall('node'):
  key=node.get('ref'),node.get('pin')
  if key in expected:schematic[key]=n.get('name').removeprefix('/')
assert expected==actual,'PCB pin-to-net mismatch'
assert expected==schematic,'Schematic pin-to-net mismatch'
rear=[f.GetReference() for f in b.GetFootprints() if f.GetLayer()==k.B_Cu]
assert set(rear)=={'TP1','TP2','TP3','TP4'},rear
pmap={p.m_Uuid.AsString():f.GetReference() for f in b.GetFootprints() for p in f.Pads()}
bad=[];pads=[p for f in b.GetFootprints() for p in f.Pads() if p.GetLayerSet().Contains(k.F_Paste)]
for v in b.GetTracks():
 if not isinstance(v,k.PCB_VIA):continue
 pos=v.GetPosition();radius=v.GetDrillValue()/2+1000
 for pad in pads:
  points=[pos]+[k.VECTOR2I(round(pos.x+radius*math.cos(i*math.pi/4)),round(pos.y+radius*math.sin(i*math.pi/4))) for i in range(8)]
  if any(pad.HitTest(point) for point in points):bad.append({'footprint':pmap[pad.m_Uuid.AsString()],'pad':pad.GetNumber(),'via_mm':[pos.x/1e6,pos.y/1e6]})
result={'pin_net_matches':len(expected),'all_real_components_front':True,'rear_copper_only':rear,'via_drill_overlaps_smd_paste_pad':bad,'track_segments':sum(not isinstance(t,k.PCB_VIA) for t in b.GetTracks()),'vias':sum(isinstance(t,k.PCB_VIA) for t in b.GetTracks())}
(root/'verification.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
assert not bad,'Unfilled drill overlaps an SMD solder pad; fix before fabrication'
