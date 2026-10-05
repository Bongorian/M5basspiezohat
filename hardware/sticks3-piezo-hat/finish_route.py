#!/usr/bin/python3
"""Complete selected DRC ratsnest edges with conservative local grid routing.
Preserves the source; generated copper MUST pass a new KiCad DRC before delivery.
Requires KiCad pcbnew, NumPy, SciPy and Pillow; see README for the C++ helper.
"""
from pathlib import Path
import argparse,json,subprocess,struct
import pcbnew as k
import numpy as np
from scipy.ndimage import distance_transform_edt
from PIL import Image,ImageDraw
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=Path);p.add_argument('drc',type=Path);p.add_argument('output',type=Path)
p.add_argument('--solver',type=Path,default=Path(__file__).resolve().parent/'.build/piezo-finish-astar');p.add_argument('--net');p.add_argument('--limit',type=int,default=99)
a=p.parse_args();src=a.source.resolve();dst=a.output.resolve()
assert src!=dst and not dst.exists(),'Preserve source and use a new output'
b=k.LoadBoard(str(src));pro=src.with_suffix('.kicad_pro').read_text()
sm=k.GetSettingsManager();assert sm.LoadProject(str(src.with_suffix('.kicad_pro')))
b.SetProject(sm.GetProject(str(src.with_suffix('.kicad_pro'))))
bb=b.GetBoardEdgesBoundingBox();origin=bb.GetOrigin();size=bb.GetSize()
# Edge drawing is 0.05 mm thick; raster geometry uses the physical outline.
ox=origin.x+25000;oy=origin.y+25000;step=25000
nx=round((size.x-50000)/step)+1;ny=round((size.y-50000)/step)+1
layers=[k.F_Cu,k.In2_Cu,k.B_Cu]
if b.GetCopperLayerCount()==6:layers=[k.F_Cu,k.In2_Cu,k.In3_Cu,k.B_Cu]
nl=len(layers);shape=(ny,nx)
clearance=round(json.loads(pro)['net_settings']['classes'][0]['clearance']*1e6)
def pt(v):return ((v.x-ox)/step,(v.y-oy)/step)
def blank():return Image.new('L',(nx,ny),0)
def draw_item(im,item,layer):
 if isinstance(item,k.PAD) and not item.GetLayerSet().Contains(layer):return
 if isinstance(item,k.PCB_TRACK) and not isinstance(item,k.PCB_VIA) and item.GetLayer()!=layer:return
 s=item.GetEffectiveShape(layer);poly=k.SHAPE_POLY_SET();s.TransformToPolygon(poly,3000,k.ERROR_OUTSIDE)
 d=ImageDraw.Draw(im)
 for j in range(poly.OutlineCount()):
  outline=poly.Outline(j);points=[pt(outline.CPoint(i)) for i in range(outline.PointCount())]
  if len(points)>=3:d.polygon(points,fill=1)
def items():return [p for f in b.GetFootprints() for p in f.Pads()]+list(b.GetTracks())
def component(item):
 c=b.GetConnectivity();seen={};todo=[item]
 while todo:
  x=todo.pop();uid=x.m_Uuid.AsString()
  if x.GetClass()=='PCB_VIA' and not isinstance(x,k.PCB_VIA):x=k.Cast_to_PCB_VIA(x)
  if uid in seen:continue
  seen[uid]=x;todo+=list(c.GetConnectedTracks(x))+list(c.GetConnectedPads(x))
 return list(seen.values())
def anchors(component_items):
 out=np.zeros((nl,ny,nx),np.uint8)
 for x in component_items:
  positions=[x.GetStart(),x.GetEnd()] if isinstance(x,k.PCB_TRACK) and not isinstance(x,k.PCB_VIA) else [x.GetPosition()]
  for l,layer in enumerate(layers):
   if isinstance(x,k.PAD) and not x.GetLayerSet().Contains(layer):continue
   if isinstance(x,k.PCB_TRACK) and not isinstance(x,k.PCB_VIA) and x.GetLayer()!=layer:continue
   for pos in positions:
    xx,yy=pt(pos);xx=round(xx);yy=round(yy)
    if 0<=xx<nx and 0<=yy<ny:out[l,yy,xx]=1
 return out
board=blank();ImageDraw.Draw(board).rounded_rectangle((0,0,nx-1,ny-1),radius=1e6/step,fill=1)
inside=distance_transform_edt(np.pad(np.array(board),1))[1:-1,1:-1]*step
work=dst.parent/(dst.stem+'-completion');work.mkdir(exist_ok=False)
edges=json.loads(a.drc.read_text())['unconnected_items'];done=[]
for edge in edges:
 if len(done)>=a.limit:break
 current={x.m_Uuid.AsString():x for x in items()}
 pair=[current.get(x['uuid']) for x in edge['items']]
 if len(pair)!=2 or any(x is None for x in pair):continue
 first,second=pair;net=first.GetNetname()
 if a.net and net!=a.net:continue
 left=component(first);right=component(second)
 if {x.m_Uuid.AsString() for x in left}&{x.m_Uuid.AsString() for x in right}:continue
 others=[x for x in items() if x.GetNetname()!=net]
 masks=[];via=np.zeros(shape,bool)
 for layer in layers:
  im=blank()
  for x in others:draw_item(im,x,layer)
  dist=distance_transform_edt(np.array(im)==0)*step
  masks.append((dist<50000+clearance+18000)|(inside<590000))
  via|=(dist<200000+clearance+33000)
 # Drills cannot intersect any SMD paste, including their own net's pads.
 paste=blank()
 for x in items():
  if isinstance(x,k.PAD) and x.GetLayerSet().Contains(k.F_Paste):draw_item(paste,x,k.F_Cu)
 via|=(distance_transform_edt(np.array(paste)==0)*step<130000)|(inside<765000)
 holes=blank();d=ImageDraw.Draw(holes)
 for x in items():
  if isinstance(x,k.PCB_VIA):radius=x.GetDrillValue()/2
  elif isinstance(x,k.PAD) and x.GetDrillSize().x:radius=max(x.GetDrillSize().x,x.GetDrillSize().y)/2
  else:continue
  xx,yy=pt(x.GetPosition());r=(radius+100000+255000)/step;d.ellipse((xx-r,yy-r,xx+r,yy+r),fill=1)
 via|=np.array(holes).astype(bool)
 forbidden=blank();d=ImageDraw.Draw(forbidden)
 for z in b.Zones():
  if z.GetIsRuleArea() and z.GetDoNotAllowVias():
   box=z.GetBoundingBox();pos=box.GetOrigin();size=box.GetSize()
   d.rectangle([pt(pos),pt(k.VECTOR2I(pos.x+size.x,pos.y+size.y))],fill=1)
 via|=distance_transform_edt(np.array(forbidden)==0)*step<245000
 blocked=np.asarray(masks,np.uint8);starts=anchors(left);goals=anchors(right)
 print('ANCHORS',net,'start',[(int(starts[i].sum()),int((starts[i]&~blocked[i]).sum())) for i in range(nl)],'goal',[(int(goals[i].sum()),int((goals[i]&~blocked[i]).sum())) for i in range(nl)],flush=True)
 inp=work/'grid.bin';path=work/'path.bin'
 with inp.open('wb') as f:
  f.write(struct.pack('iii',nx,ny,nl));f.write(blocked.tobytes());f.write(via.astype(np.uint8).tobytes());f.write(starts.tobytes());f.write(goals.tobytes())
 result=subprocess.run([str(a.solver.resolve()),str(inp),str(path)],capture_output=True,text=True)
 print(net,result.stderr.strip(),flush=True)
 if result.returncode:continue
 indices=np.fromfile(path,dtype=np.int32);points=[]
 for index in indices:
  l=int(index)//(nx*ny);r=int(index)%(nx*ny);points.append((l,r%nx,r//nx))
 # Keep changes of direction and layer; straight grid runs become one segment.
 compact=[points[0]]
 for i in range(1,len(points)-1):
  before=tuple(points[i][j]-points[i-1][j] for j in range(3));after=tuple(points[i+1][j]-points[i][j] for j in range(3))
  if before!=after:compact.append(points[i])
 compact.append(points[-1]);new=[]
 def position(v):return k.VECTOR2I(ox+v[1]*step,oy+v[2]*step)
 for q,r in zip(compact,compact[1:]):
  if q[0]!=r[0]:
   v=k.PCB_VIA(b);v.SetPosition(position(q));v.SetWidth(400000);v.SetDrill(200000);v.SetViaType(k.VIATYPE_THROUGH);v.SetLayerPair(k.F_Cu,k.B_Cu);v.SetNetCode(first.GetNetCode());b.Add(v);new.append(v)
  elif position(q)!=position(r):
   t=k.PCB_TRACK(b);t.SetStart(position(q));t.SetEnd(position(r));t.SetWidth(100000);t.SetLayer(layers[q[0]]);t.SetNetCode(first.GetNetCode());b.Add(t);new.append(t)
 b.BuildConnectivity();done.append({'net':net,'added_items':len(new)})
k.ZONE_FILLER(b).Fill(b.Zones());k.SaveBoard(str(dst),b);dst.with_suffix('.kicad_pro').write_text(pro)
(work/'result.json').write_text(json.dumps(done,indent=2))
subprocess.run(['kicad-cli','pcb','drc','--format','json','-o',str(work/'drc.json'),str(dst)],check=True)
report=json.loads((work/'drc.json').read_text());print('FINAL',len(report['unconnected_items']),'unconnected;',len(report['violations']),'DRC violations')
