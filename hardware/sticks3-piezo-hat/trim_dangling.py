import pcbnew as k,json,sys
from pathlib import Path
src=Path(sys.argv[1]).resolve();drc=Path(sys.argv[2]);dst=Path(sys.argv[3]).resolve();b=k.LoadBoard(str(src));pro=src.with_suffix('.kicad_pro').read_text();sm=k.GetSettingsManager();sm.LoadProject(str(src.with_suffix('.kicad_pro')));b.SetProject(sm.GetProject(str(src.with_suffix('.kicad_pro'))))
def count(net):
 c=b.GetConnectivity();seen=set();groups=0;plane=False
 for f in b.GetFootprints():
  for pad in f.Pads():
   if pad.GetNetname()!=net or pad.m_Uuid.AsString() in seen:continue
   todo=[pad];ids=set();tied=False
   while todo:
    x=todo.pop();u=x.m_Uuid.AsString()
    if u in ids:continue
    ids.add(u);tied|=x.GetClass()=='PCB_VIA' or (x.GetClass()=='PAD' and x.GetLayerSet().Contains(k.In1_Cu));todo+=list(c.GetConnectedTracks(x))+list(c.GetConnectedPads(x))
   seen|=ids
   if net=='GND' and tied:plane=True
   else:groups+=1
 return groups+int(plane)
tracks={x.m_Uuid.AsString():x for x in b.GetTracks()};r=json.loads(drc.read_text());removed=0;kept=[]
for v in r['violations']:
 if v['type'] not in ['track_dangling','via_dangling']:continue
 x=tracks.get(v['items'][0]['uuid'])
 if x is None:continue
 if x.GetNetname()=='GND' and isinstance(x,k.PCB_VIA):continue
 before=count(x.GetNetname());b.Remove(x);b.BuildConnectivity();after=count(x.GetNetname())
 if after>before:b.Add(x);b.BuildConnectivity();kept.append(x.m_Uuid.AsString())
 else:removed+=1
b.BuildConnectivity();k.ZONE_FILLER(b).Fill(b.Zones());k.SaveBoard(str(dst),b);dst.with_suffix('.kicad_pro').write_text(pro);print('Trimmed',removed,'protected',len(kept),kept)
