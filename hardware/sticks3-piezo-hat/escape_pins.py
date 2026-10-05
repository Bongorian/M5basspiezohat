#!/usr/bin/python3
"""Reserve straight/angled IC escapes, rip up only obstructing copper.
Results are intermediate; complete all affected nets and validate with DRC.
"""
from pathlib import Path
import argparse,json,math,subprocess
import pcbnew as k
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path)
p.add_argument('--plan-only',action='store_true');p.add_argument('--pins',default='U3:15,U3:19,U3:20,U3:27,U4:15,U4:3,U3:5,U3:29,U4:19,U4:28,U4:31,U2:10,U3:22')
p.add_argument('--preserve-escapes',type=Path)
p.add_argument('--directions-any',action='store_true')
p.add_argument('--protect-net',default='')
a=p.parse_args();src=a.source.resolve();dst=a.output.resolve();assert src!=dst and not dst.exists()
b=k.LoadBoard(str(src));pro=src.with_suffix('.kicad_pro').read_text();sm=k.GetSettingsManager();assert sm.LoadProject(str(src.with_suffix('.kicad_pro')));b.SetProject(sm.GetProject(str(src.with_suffix('.kicad_pro'))))
cu=[k.F_Cu,k.In1_Cu,k.In2_Cu,k.B_Cu]
def vec(x,y):return k.VECTOR2I(round(x),round(y))
def distance(p,q):return math.hypot(p.x-q.x,p.y-q.y)
def bbox_hits(shape,other,margin):
 box=shape.BBox();box.Inflate(margin);return box.Intersects(other.BBox())
records=[];reserved={t.m_Uuid.AsString() for t in b.GetTracks() if t.GetNetname() in a.protect_net.split(',')}
if a.preserve_escapes:
 for record in json.loads(a.preserve_escapes.read_text()):
  ref,pn=record['pin'].split(':');p0=next(p for f in b.GetFootprints() for p in f.Pads() if f.GetReference()==ref and p.GetNumber()==pn)
  end=vec(record['via_mm'][0]*1e6,record['via_mm'][1]*1e6)
  for item in b.GetTracks():
   if item.GetNetname()!=record['net']:continue
   if isinstance(item,k.PCB_VIA):protect=distance(item.GetPosition(),end)<1000
   else:protect=item.GetLayer()==k.F_Cu and (distance(item.GetStart(),p0.GetPosition())<1000 or distance(item.GetEnd(),p0.GetPosition())<1000 or distance(item.GetStart(),end)<1000 or distance(item.GetEnd(),end)<1000)
   if protect:reserved.add(item.m_Uuid.AsString())
escape_corridors=[]
for key in a.pins.split(',')+[f'{f.GetReference()}:{p.GetNumber()}' for f in b.GetFootprints() if f.GetReference() in ['U3','U4'] for p in f.Pads() if p.GetNumber()!='33']:
 ref,pn=key.split(':');p0=next(p for f in b.GetFootprints() for p in f.Pads() if f.GetReference()==ref and p.GetNumber()==pn)
 base=p0.GetPosition();centre=p0.GetParent().GetPosition();horizontal=abs(base.x-centre.x)>abs(base.y-centre.y)
 dx=(1 if base.x>centre.x else -1) if horizontal else 0;dy=0 if horizontal else (1 if base.y>centre.y else -1)
 sh=p0.GetEffectiveShape(k.F_Cu);bb=sh.BBox();half=(bb.GetSize().x if horizontal else bb.GetSize().y)/2
 end=vec(base.x+dx*(half+375000),base.y+dy*(half+375000))
 escape_corridors.append((p0.GetNetname(),k.SHAPE_SEGMENT(base,end,100000)))
for key in a.pins.split(','):
 ref,pn=key.split(':');pad=next(p for f in b.GetFootprints() for p in f.Pads() if f.GetReference()==ref and p.GetNumber()==pn);net=pad.GetNetname();base=pad.GetPosition();centre=pad.GetParent().GetPosition();horizontal=abs(base.x-centre.x)>abs(base.y-centre.y);dx=(1 if base.x>centre.x else -1) if horizontal else 0;dy=0 if horizontal else (1 if base.y>centre.y else -1)
 sh=pad.GetEffectiveShape(k.F_Cu);bb=sh.BBox();half=(bb.GetSize().x if horizontal else bb.GetSize().y)/2
 waypoint=vec(base.x+dx*(half+175000),base.y+dy*(half+175000))
 pads=[p for f in b.GetFootprints() for p in f.Pads()];tracks=list(b.GetTracks());fixed=[];paste=[]
 for item in pads:
  if item.GetNetname()!=net:
   fixed.extend((l,item.GetEffectiveShape(l)) for l in cu if item.GetLayerSet().Contains(l))
  if item.GetLayerSet().Contains(k.F_Paste):paste.append(item.GetEffectiveShape(k.F_Cu))
 movable=[]
 for item in tracks:
  if item.GetNetname()==net:continue
  if isinstance(item,k.PCB_VIA):ls=cu
  else:ls=[item.GetLayer()]
  shapes=[(l,item.GetEffectiveShape(l)) for l in ls]
  if item.m_Uuid.AsString() in reserved:fixed.extend(shapes)
  else:movable.append((item,shapes))
 zones=[z.GetBoundingBox() for z in b.Zones() if z.GetIsRuleArea() and z.GetDoNotAllowVias()]
 best=None;checked=0
 # Start outside solder pads; stagger adjacent 0.4 mm IC pins as needed.
 directions=[(dx,dy)]
 if a.directions_any:directions+= [q for q in [(1,0),(-1,0),(0,1),(0,-1)] if q!=(dx,dy)]
 for dx,dy in directions:
  horizontal=bool(dx);sh=pad.GetEffectiveShape(k.F_Cu);bb=sh.BBox();half=(bb.GetSize().x if horizontal else bb.GetSize().y)/2
  waypoint=vec(base.x+dx*(half+175000),base.y+dy*(half+175000))
  for outward in range(125000,1700001,50000):
   for lateral in [0]+[sgn*v for v in range(50000,650001,50000) for sgn in [1,-1]]:
    pos=vec(base.x+dx*(half+outward)+(0 if horizontal else lateral),base.y+dy*(half+outward)+(lateral if horizontal else 0))
    pos=vec(round(pos.x/25000)*25000,round(pos.y/25000)*25000)
    if not(40775000<=pos.x<=60200000 and 30775000<=pos.y<=67200000):continue
    if any(z.GetLeft()-230000<=pos.x<=z.GetRight()+230000 and z.GetTop()-230000<=pos.y<=z.GetBottom()+230000 for z in zones):continue
    circle=k.SHAPE_CIRCLE(pos,200000);drill=k.SHAPE_CIRCLE(pos,100000)
    segments=[k.SHAPE_SEGMENT(base,waypoint,100000),k.SHAPE_SEGMENT(waypoint,pos,100000)]
    if any(bbox_hits(s,drill,12000) and s.Collide(drill,12000) for s in paste):continue
    if any(othernet!=net and bbox_hits(corridor,circle,112000) and corridor.Collide(circle,112000) for othernet,corridor in escape_corridors):continue
    if any((bbox_hits(s,circle,112000) and s.Collide(circle,112000)) or (l==k.F_Cu and any(bbox_hits(s,seg,112000) and s.Collide(seg,112000) for seg in segments)) for l,s in fixed):continue
    if any(p.GetDrillSize().x and distance(p.GetPosition(),pos)<max(p.GetDrillSize().x,p.GetDrillSize().y)/2+355000 for p in pads):continue
    collisions=[]
    for item,shapes in movable:
     hit=any((bbox_hits(s,circle,112000) and s.Collide(circle,112000)) or (l==k.F_Cu and any(bbox_hits(s,seg,112000) and s.Collide(seg,112000) for seg in segments)) for l,s in shapes)
     if isinstance(item,k.PCB_VIA) and distance(item.GetPosition(),pos)<450000:hit=True
     if hit:collisions.append(item)
    if any(isinstance(t,k.PCB_VIA) and t.GetNetname()==net and distance(t.GetPosition(),pos)<450000 for t in tracks):continue
    cost=sum((15 if isinstance(t,k.PCB_VIA) else 1)+(4 if t.GetNetname()=='GND' else 0) for t in collisions)+(outward+abs(lateral))/1e6*.3
    if best is None or cost<best[0]:best=(cost,pos,collisions,waypoint)
    checked+=1
   if best is not None and best[0]<.55:break
 if best is None:print(key,net,'NO ESCAPE',flush=True);continue
 cost,pos,collisions,waypoint=best;record={'pin':key,'net':net,'via_mm':[pos.x/1e6,pos.y/1e6],'ripped':[{'uuid':t.m_Uuid.AsString(),'net':t.GetNetname(),'type':t.GetClass()} for t in collisions]};records.append(record);print(key,net,record['via_mm'],'rip',[(t.GetNetname(),t.GetClass()) for t in collisions],flush=True)
 if a.plan_only:continue
 for t in collisions:b.Remove(t)
 v=k.PCB_VIA(b);v.SetPosition(pos);v.SetWidth(400000);v.SetDrill(200000);v.SetViaType(k.VIATYPE_THROUGH);v.SetLayerPair(k.F_Cu,k.B_Cu);v.SetNetCode(pad.GetNetCode());b.Add(v);reserved.add(v.m_Uuid.AsString())
 for start,end in [(base,waypoint),(waypoint,pos)]:
  if start==end:continue
  t=k.PCB_TRACK(b);t.SetStart(start);t.SetEnd(end);t.SetLayer(k.F_Cu);t.SetWidth(100000);t.SetNetCode(pad.GetNetCode());b.Add(t);reserved.add(t.m_Uuid.AsString())
 b.BuildConnectivity()
if not a.plan_only:
 k.ZONE_FILLER(b).Fill(b.Zones());k.SaveBoard(str(dst),b);dst.with_suffix('.kicad_pro').write_text(pro);dst.with_suffix('.escape.json').write_text(json.dumps(records,indent=2));subprocess.run(['kicad-cli','pcb','drc','--format','json','-o',str(dst.with_suffix('.drc.json')),str(dst)],check=True)
