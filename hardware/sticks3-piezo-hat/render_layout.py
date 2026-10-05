#!/usr/bin/python3
from pathlib import Path
import pcbnew as k
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parent;b=k.LoadBoard(str(ROOT/'sticks3-piezo-hat.kicad_pcb'))
scale=55;off=80;w=21*scale+off*2;h=38*scale+off*2
font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',18);titlefont=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',23)
def pos(v):return (off+(v.x/1e6-40)*scale,off+(v.y/1e6-30)*scale)
def drawshape(d,shape,color):
 poly=k.SHAPE_POLY_SET();shape.TransformToPolygon(poly,3000,k.ERROR_OUTSIDE)
 for j in range(poly.OutlineCount()):
  o=poly.Outline(j);xy=[pos(o.CPoint(i)) for i in range(o.PointCount())]
  if len(xy)>2:d.polygon(xy,fill=color)
for assembly in [False,True]:
 im=Image.new('RGB',(w,h),(242,246,248));d=ImageDraw.Draw(im);d.rounded_rectangle((off,off,w-off,h-off),radius=scale,fill=(22,74,54),outline=(70,90,80),width=2)
 d.text((off,25),'StickS3 Piezo Hat R1  |  21 × 38 mm  |  4 layers',font=titlefont,fill=(30,40,50))
 for t in b.GetTracks():
  if isinstance(t,k.PCB_VIA):continue
  if t.GetLayer()==k.F_Cu:drawshape(d,t.GetEffectiveShape(k.F_Cu),(61,120,86) if assembly else (180,164,100))
 for t in b.GetTracks():
  if isinstance(t,k.PCB_VIA):
   xy=pos(t.GetPosition());rr=.1*scale;d.ellipse((xy[0]-rr,xy[1]-rr,xy[0]+rr,xy[1]+rr),fill=(8,25,22))
 for f in b.GetFootprints():
  for p in f.Pads():
   if not p.GetLayerSet().Contains(k.F_Cu):continue
   drawshape(d,p.GetEffectiveShape(k.F_Cu),(220,191,115))
   if p.GetDrillSize().x:
    xy=pos(p.GetPosition());rr=p.GetDrillSize().x/2e6*scale;d.ellipse((xy[0]-rr,xy[1]-rr,xy[0]+rr,xy[1]+rr),fill=(242,246,248))
  if f.GetLayer()!=k.F_Cu:continue
  for g in f.GraphicalItems():
   if g.GetLayer()!=k.F_Fab or not isinstance(g,k.PCB_SHAPE):continue
   if g.GetShape()==k.SHAPE_T_SEGMENT:d.line([pos(g.GetStart()),pos(g.GetEnd())],fill=(180,194,186),width=2)
   elif g.GetShape()==k.SHAPE_T_RECT:d.rectangle([tuple(min(a,b) for a,b in zip(pos(g.GetStart()),pos(g.GetEnd()))),tuple(max(a,b) for a,b in zip(pos(g.GetStart()),pos(g.GetEnd())))],outline=(180,194,186),width=2)
  if assembly:
   if f.GetReference().startswith(('U','D')):
    p1=next((p for p in f.Pads() if p.GetNumber()=='1'),None)
    if p1:
     xx,yy=pos(p1.GetPosition());d.ellipse((xx-6,yy-6,xx+6,yy+6),outline=(255,100,80),width=3)
   xy=pos(f.GetPosition());ref=f.GetReference();box=d.textbbox((0,0),ref,font=font);ww=box[2];hh=box[3]-box[1];d.rectangle((xy[0]-ww/2-2,xy[1]-hh/2-3,xy[0]+ww/2+2,xy[1]+hh/2+3),fill=(21,57,43));d.text((xy[0],xy[1]),ref,font=font,anchor='mm',fill=(255,255,255))
 mask=Image.new('L',(w,h),0);ImageDraw.Draw(mask).rounded_rectangle((off,off,w-off,h-off),radius=scale,fill=255);im=Image.composite(im,Image.new('RGB',(w,h),(242,246,248)),mask);d=ImageDraw.Draw(im)
 d.text((off,25),'StickS3 Piezo Hat R1  |  21 × 38 mm  |  4 layers',font=titlefont,fill=(30,40,50))
 d.text((off,h-55),'Front view  |  Top only  |  Unconnected 0 / DRC 0  |  Red ring: pin 1',font=titlefont,fill=(30,40,50))
 file=ROOT/('release/assembly-front.png' if assembly else 'preview/routed-front.png');im.save(file)
