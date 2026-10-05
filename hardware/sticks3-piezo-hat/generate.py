#!/usr/bin/python3
"""Generate editable KiCad prototype sources, BOM and connectivity manifest.
Run with /usr/bin/python3 (KiCad 9 pcbnew). Sources remain the design authority.
No claim of measured analogue performance or verified host connector mating.
"""
from pathlib import Path
from collections import defaultdict
import json, csv, math, uuid
import numpy as np
import pcbnew as k

OUT=Path(__file__).resolve().parent
NAME='sticks3-piezo-hat'
LIB=OUT/'PiezoHat.pretty'
LIB.mkdir(exist_ok=True)
ORIGIN=(40,30)
W,H,R=21,38,1
ROOT=str(uuid.uuid5(uuid.NAMESPACE_URL,NAME))
def uid(s): return str(uuid.uuid5(uuid.NAMESPACE_URL,NAME+'/'+s))
def mm(x): return k.FromMM(x)
def pos(x,y): return k.VECTOR2I(mm(x+ORIGIN[0]),mm(y+ORIGIN[1]))
def q(s): return json.dumps(str(s),ensure_ascii=False)

parts={
 'OP':('TLV9064IRTER','C882406','Package_DFN_QFN:QFN-16-1EP_3x3mm_P0.5mm_EP1.675x1.675mm','JLC','check at BOM upload',.8868),
 'ADC':('ES7210','C365743','PiezoHat:ES7210_QFN32_4x4_P0.4_EP2.7','JLC','Extended',.9057),
 'LDO':('AP2112K-3.3TRG1','C51118','Package_TO_SOT_SMD:SOT-23-5','JLC','check at BOM upload',.1726),
 'CLAMP':('BAV199W (CBI)','C51315120','Package_TO_SOT_SMD:SOT-323_SC-70','HAND','not needed for PCBA',.0336),
 'R1k':('0402WGF1001TCE','C11702','Resistor_SMD:R_0402_1005Metric','JLC','Basic observed',None),
 'R10M':('1RC0402J0106','C54531017','Resistor_SMD:R_0402_1005Metric','JLC','check at BOM upload',.0012),
 'R47k':('0603WAF4702T5E','C25819','Resistor_SMD:R_0603_1608Metric','JLC','check at BOM upload',None),
 'R33':('0402WGF330JTCE','C25105','Resistor_SMD:R_0402_1005Metric','JLC','check at BOM upload',None),
 'C100n':('CL05B104KO5NNNC','C1525','Capacitor_SMD:C_0402_1005Metric','JLC','check at BOM upload',None),
 'C47n':('0402B473K500NT','C82219','Capacitor_SMD:C_0402_1005Metric','JLC','check at BOM upload',None),
 'C1u':('CL05A105KA5NQNC','C52923','Capacitor_SMD:C_0402_1005Metric','JLC','check at BOM upload',None),
 'C10u':('CL10A106KP8NNNC','C19702','Capacitor_SMD:C_0603_1608Metric','JLC','check at BOM upload',.0322),
 'HAT':('BXCONN PZ-2.54-2*08-8.5-WZ','C53207371','Connector_PinHeader_2.54mm:PinHeader_2x08_P2.54mm_Horizontal','HAND','not needed for PCBA',.0639),
 'PIEZO':('Internal wire solder pair','','PiezoHat:PiezoPair_P3.8','HAND','board pads only',None),
 'TP':('Test point','','TestPoint:TestPoint_Pad_D1.0mm','NONE','board copper only',None),
}

board=k.BOARD()
board.SetCopperLayerCount(4)
ds=board.GetDesignSettings()
ds.SetBoardThickness(mm(1))
ds.m_MinClearance=mm(.1)
ds.m_TrackMinWidth=mm(.1)
ds.m_CopperEdgeClearance=mm(.5)
ds.m_SolderMaskToCopperClearance=0
ds.m_SolderMaskMinWidth=mm(.10)
ds.m_SilkClearance=mm(.15)
ds.SetCustomTrackWidth(mm(.127))
net={}
def getnet(s):
 if s is None: return None
 if s not in net:
  n=k.NETINFO_ITEM(board,s,len(net)+1); board.Add(n);net[s]=n
 return net[s]

# Custom QFN uses conservative EP geometry common to supplied package revisions.
fp=k.FootprintLoad('/usr/share/kicad/footprints/Package_DFN_QFN.pretty','QFN-32-1EP_4x4mm_P0.4mm_EP2.65x2.65mm')
fp.SetReference('U'); fp.SetValue('ES7210')
fp.SetFPID(k.LIB_ID('PiezoHat','ES7210_QFN32_4x4_P0.4_EP2.7'))
for p in fp.Pads():
 if p.GetNumber()=='33': p.SetSize(k.VECTOR2I(mm(2.7),mm(2.7)))
 p.SetLocalSolderMaskMargin(0)
for g in fp.GraphicalItems():
 if g.GetLayer()==k.F_SilkS: g.SetLayer(k.F_Fab)
k.PCB_IO_MGR.PluginFind(k.PCB_IO_MGR.KICAD_SEXP).FootprintSave(str(LIB),fp)
# Round through-hole wire pads; front-only assembly still allows reverse solder.
fp=k.FOOTPRINT(board)
fp.SetFPID(k.LIB_ID('PiezoHat','PiezoPair_P3.8'))
fp.SetReference('J');fp.SetValue('SIG/GND')
fp.SetAttributes(k.FP_THROUGH_HOLE)
for n,y in [('1',0),('2',2.5)]:
 p=k.PAD(fp);p.SetNumber(n);p.SetShape(k.PAD_SHAPE_CIRCLE)
 p.SetAttribute(k.PAD_ATTRIB_PTH);p.SetSize(k.VECTOR2I(mm(1.8),mm(1.8)))
 p.SetDrillSize(k.VECTOR2I(mm(.8),mm(.8)))
 p.SetLayerSet(k.PAD.PTHMask());p.SetPosition(k.VECTOR2I(0,mm(y)));fp.Add(p)
k.PCB_IO_MGR.PluginFind(k.PCB_IO_MGR.KICAD_SEXP).FootprintSave(str(LIB),fp)
(OUT/'fp-lib-table').write_text('(fp_lib_table (version 7) (lib (name "PiezoHat") (type "KiCad") (uri "${KIPRJMOD}/PiezoHat.pretty") (options "") (descr "Project footprints")))\n')
components=[]; fps={}; counters=defaultdict(int)
def add(ref,key,value,pins,x,y,angle=0,group='misc'):
 mpn,code,fid,assembly,partclass,price=parts[key]
 lib,foot=fid.split(':')
 path=str(LIB) if lib=='PiezoHat' else '/usr/share/kicad/footprints/'+lib+'.pretty'
 f=k.FootprintLoad(path,foot)
 assert f is not None,(fid,ref)
 # Mechanical proxy has the same 3x3 body; exact TLV model is not bundled.
 if key=='OP':
  models=list(f.Models());f.Models().clear()
  for model in models:
   model.m_Filename='${KICAD9_3DMODEL_DIR}/Package_DFN_QFN.3dshapes/QFN-16-1EP_3x3mm_P0.5mm_EP1.7x1.7mm.step'
   f.Add3DModel(model)
 if key=='HAT':
  for pad in f.Pads(): pad.SetDrillSize(k.VECTOR2I(mm(1.02),mm(1.02)))
  # BXCONN drawing: 20.12 +/-0.30 body; 6.9 +/-0.2 + 6.0 +/-0.2
  # from rear hole row to mating tip. Courtyard includes body maximum +0.25.
  # PCB-side occupied area ends at the TH pad +0.25, not a generic proxy margin.
  for g in list(f.GraphicalItems()):
   if g.GetLayer()==k.F_CrtYd: f.Remove(g)
  g=k.PCB_SHAPE(f);g.SetShape(k.SHAPE_T_RECT);g.SetLayer(k.F_CrtYd);g.SetWidth(mm(.05));g.SetStart(k.VECTOR2I(mm(-1.1),mm(-1.57)));g.SetEnd(k.VECTOR2I(mm(13.55),mm(19.35)));f.Add(g)
 # Local footprint variants keep edited fab graphics/mask settings auditable.
 for g in f.GraphicalItems():
  if g.GetLayer()==k.F_SilkS: g.SetLayer(k.F_Fab)
 for p in f.Pads(): p.SetLocalSolderMaskMargin(0)
 dense=foot if lib=='PiezoHat' else foot+'_AssemblyFront'
 f.SetFPID(k.LIB_ID('PiezoHat',dense))
 k.PCB_IO_MGR.PluginFind(k.PCB_IO_MGR.KICAD_SEXP).FootprintSave(str(LIB),f)
 fid='PiezoHat:'+dense
 f.SetReference(ref);f.SetValue(value)
 f.SetPosition(pos(x,y));f.SetOrientationDegrees(angle)
 f.SetPath(k.KIID_PATH('/'+ROOT+'/'+uid(ref)))
 f.Reference().SetVisible(False);f.Value().SetVisible(False)
 f.SetExcludedFromPosFiles(assembly!='JLC')
 f.SetExcludedFromBOM(assembly=='NONE')
 for p in f.Pads():
  n=pins.get(p.GetNumber())
  if n is not None: p.SetNet(getnet(n))
  p.SetLocalSolderMaskMargin(0)
 board.Add(f);fps[ref]=f
 components.append(dict(ref=ref,key=key,value=value,mpn=mpn,lcsc=code,
   footprint=fid,assembly=assembly,part_class=partclass,reference_price=price,
   pins={str(a):b for a,b in pins.items()},x=x,y=y,angle=angle,group=group,uuid=uid(ref)))
 return ref
def passive(prefix,key,value,a,b,x,y,angle=0,group='misc'):
 counters[prefix]+=1
 return add(prefix+str(counters[prefix]),key,value,{'1':a,'2':b},x,y,angle,group)
def cap(key,value,a,x,y,angle=0,group='misc',b='GND'):
 return passive('C',key,value,a,b,x,y,angle,group)

# Five individually biased inputs. Each 20M load is two identical 10M parts.
for i in range(1,6):
 x=2.9+(i-1)*3.8;g='input'+str(i)
 add('J'+str(i),'PIEZO','PIEZO'+str(i),{'1':f'PZ{i}','2':'GND'},x,1.8,0,g)
 passive('R','R47k','47k',f'PZ{i}',f'HI{i}',x,6.25,0,g)
 passive('R','R10M','10M',f'HI{i}',f'BIAS{i}',x,7.8,0,g)
 passive('R','R10M','10M',f'BIAS{i}','VMID',x,9.05,0,g)
 add('D'+str(i),'CLAMP','BAV199W',{'1':'GND','2':'3V3A','3':f'HI{i}'},x,11.7,90,g)

opnames={1:'INA+',2:'V+',3:'INB+',4:'INB-',5:'OUTB',6:'NC',7:'NC',8:'OUTC',9:'INC-',10:'INC+',11:'V-',12:'IND+',13:'IND-',14:'OUTD',15:'OUTA',16:'INA-',17:'EP/V-'}
oppins={2:'3V3A',11:'GND',17:'GND'}
for i,(o,m,p) in enumerate([(15,16,1),(5,4,3),(8,9,10),(14,13,12)],1):
 oppins[o]=oppins[m]=f'BUF{i}';oppins[p]=f'HI{i}'
add('U1','OP','TLV9064IRTER',{str(n):v for n,v in oppins.items()},5.55,16.7,0,'buffers')
oppins={15:'BUF5',16:'BUF5',1:'HI5',2:'3V3A',3:'VDIV',4:'VMID',5:'VMID',8:'SPARE_C',9:'SPARE_C',10:'VDIV',11:'GND',12:'VDIV',13:'SPARE_D',14:'SPARE_D',17:'GND'}
add('U2','OP','TLV9064IRTER',{str(n):v for n,v in oppins.items()},15.7,16.7,0,'buffers')
cap('C100n','100n','3V3A',1.0,16.7,90,'buffers')
cap('C100n','100n','3V3A',10.7,16.7,90,'buffers')
# Per-string low-pass and DC isolation. ADC N gets its own AC ground capacitor.
for i in range(1,6):
 x=2.9+(i-1)*3.8;g='input'+str(i)
 passive('R','R1k','1k',f'BUF{i}',f'LP{i}A',x-.85,21.8,90,g)
 cap('C47n','47n',f'LP{i}A',x+.9,21.8,90,g)
 passive('R','R1k','1k',f'LP{i}A',f'LP{i}B',x-.85,24.05,90,g)
 cap('C47n','47n',f'LP{i}B',x+.9,24.05,90,g)
 passive('C','C10u','10u',f'LP{i}B',f'AIN{i}P',x,26.25,0,g)
 cap('C10u','10u',f'AIN{i}N',x,28.5,0,g)

adc_names={1:'AD0',2:'AD1',3:'CDATA',4:'CCLK',5:'MCLK',6:'VDDP',7:'VDDD',8:'GNDD',9:'SCLK',10:'LRCK',11:'TDMOUT',12:'TDMIN',13:'INT',14:'DMIC_CLK',15:'MIC1N',16:'MIC1P',17:'REFP12',18:'REFQ12',19:'MIC2P',20:'MIC2N',21:'GNDA',22:'VDDA',23:'VDDM',24:'MICBIAS12',25:'REFQM',26:'MICBIAS34',27:'MIC4N',28:'MIC4P',29:'REFP34',30:'REFQ34',31:'MIC3P',32:'MIC3N',33:'EP'}
for index,x in [(3,5.2),(4,15.8)]:
 pins={1:'GND' if index==3 else '3V3D',2:'GND',3:'SDA',4:'SCL',5:'MCLK',6:'3V3D',7:'3V3D',8:'GND',9:'BCLK',10:'WS',11:'CASCADE' if index==3 else 'TDM_OUT',12:'GND' if index==3 else 'CASCADE_IN',13:f'INT{index}',14:None,21:'GND',22:'3V3A',23:'3V3A',33:'GND'}
 for p in [17,18,24,25,26,29,30]: pins[p]=f'U{index}_{adc_names[p]}'
 for ch,(p,n) in enumerate([(16,15),(19,20),(31,32),(28,27)],1):
  channel=ch if index==3 else ch+4
  pins[p]=f'AIN{channel}P'; pins[n]=f'AIN{channel}N'
 add('U'+str(index),'ADC','ES7210',{str(p):v for p,v in pins.items()},x,31.5,0,'adc'+str(index))
 # Seven reference and bias caps; all fitted even when MICBIAS is not used.
 sites=[(x-3.45,29.0),(x-3.45,30.6),(x-3.45,32.2),(x-3.45,33.8),(x+3.45,29.0),(x+3.45,30.6),(x+3.45,32.2)]
 for p,(cx,cy) in zip([17,18,24,25,26,29,30],sites): cap('C1u','1u',pins[p],cx,cy,90,'adc'+str(index))
 for key,value,power,cx,cy in [('C1u','1u','3V3A',x-1.2,35.0),('C1u','1u','3V3A',x+1.2,35.0),('C100n','100n','3V3D',x-1.2,28.6),('C100n','100n','3V3D',x+1.2,28.6)]:
  cap(key,value,power,cx,cy,0,'adc'+str(index))
 passive('R','R47k','47k',pins[13],'3V3D',x+3.45,33.8,90,'adc'+str(index))
# Unused ADC inputs AC grounded, firmware powers down their analogue channels.
for i,x in [(6,12.8),(7,15.8),(8,18.8)]:
 cap('C1u','1u',f'AIN{i}P',x,36.8,0,'unused')
 cap('C1u','1u',f'AIN{i}N',x,38.3,0,'unused')

# Quiet analogue and digital supplies use one shared LDO part number.
for ref,x,out in [('U5',3.0,'3V3A'),('U6',8.1,'3V3D')]:
 add(ref,'LDO','AP2112K-3.3TRG1',{'1':'5V_HAT','2':'GND','3':'5V_HAT','4':None,'5':out},x,36.9,0,'power')
 cap('C10u','10u','5V_HAT',x-1.1,39.1,0,'power')
 cap('C10u','10u',out,x+1.1,39.1,0,'power')

# Midpoint divider is filtered BEFORE the buffer; no large cap on op-amp output.
passive('R','R47k','47k','3V3A','VDIV',10.5,12.6,90,'midpoint')
passive('R','R47k','47k','VDIV','GND',10.5,14.7,90,'midpoint')
cap('C1u','1u','VDIV',10.5,16.6,90,'midpoint')
passive('R','R1k','1k','SDA','3V3D',10.5,18.2,90,'digital')
passive('R','R1k','1k','SCL','3V3D',10.5,19.8,90,'digital')
for a,b,x,y in [('MCLK_HAT','MCLK',1.8,41.0),('BCLK_HAT','BCLK',4.1,41.0),('WS_HAT','WS',6.4,41.0),('TDM_OUT','TDM_HAT',18.5,35.0),('CASCADE','CASCADE_IN',10.5,31.5)]:
 passive('R','R33','33R',a,b,x,y,0,'digital')

hatpins={'1':'GND','2':'SCL','3':'5V_HAT','4':'SDA','5':None,'6':'WS_HAT','7':'MCLK_HAT','8':'BCLK_HAT','9':'TDM_HAT','10':None,'11':None,'12':None,'13':None,'14':None,'15':None,'16':None}
add('J6','HAT','HAT2_MANUAL',hatpins,19.39,41.0,270,'host')
for i,(value,x,y) in enumerate([('GND',10.5,35.0),('3V3A',10.5,37.0),('3V3D',10.5,39.0),('VMID',10.5,10.8)],1):
 add('TP'+str(i),'TP',value,{'1':value},x,y,0,'test')

# Four rounded corners: exact lines and quarter-circle arcs, no chamfers.
def edge_line(a,b):
 s=k.PCB_SHAPE();s.SetShape(k.SHAPE_T_SEGMENT);s.SetStart(pos(*a));s.SetEnd(pos(*b));s.SetLayer(k.Edge_Cuts);s.SetWidth(mm(.05));board.Add(s)
def arc(a,m,b):
 s=k.PCB_SHAPE();s.SetShape(k.SHAPE_T_ARC);s.SetArcGeometry(pos(*a),pos(*m),pos(*b));s.SetLayer(k.Edge_Cuts);s.SetWidth(mm(.05));board.Add(s)
edge_line((R,0),(W-R,0));edge_line((W,R),(W,H-R));edge_line((W-R,H),(R,H));edge_line((0,H-R),(0,R))
t=R*(1-1/math.sqrt(2))
arc((W-R,0),(W-t,t),(W,R));arc((W,H-R),(W-t,H-t),(W-R,H))
arc((R,H),(t,H-t),(0,H-R));arc((0,R),(t,t),(R,0))

# Identity marks on assembly drawings; uncluttered production silk.
def pcbtext(label,x,y,size=.8,layer=k.F_SilkS,angle=0):
 t=k.PCB_TEXT(board);t.SetText(label);t.SetPosition(pos(x,y));t.SetTextSize(k.VECTOR2I(mm(size),mm(size)));t.SetTextThickness(mm(.15) if layer in [k.F_SilkS,k.B_SilkS] else mm(.12));t.SetLayer(layer);t.SetTextAngle(k.EDA_ANGLE(angle,k.DEGREES_T));t.SetMirrored(layer in [k.B_SilkS,k.B_Fab]);board.Add(t)
for i in range(1,6): pcbtext(str(i),2.9+(i-1)*3.8,5.8,1.0,k.B_SilkS)
pcbtext('PZ-HAT R1',10.5,17.5,.8,k.B_Fab)

# Compact R1 placement. Bare test copper moves to the back, no rear components.
major={'U1':(5.55,13.75),'U2':(15.7,13.75),'U3':(5.2,25.5),'U4':(15.8,25.5),
       'U5':(3.1,30.25),'U6':(8.1,30.25),'J6':(19.39,33.1)}
for c in components:
 if c['ref'] in major: c['x'],c['y']=major[c['ref']]
 elif c['key']=='PIEZO': c['y']=1.6
 elif c['key']=='CLAMP': c['y']=8.7
 elif c['group'].startswith('input'):
  c['y']={6.25:5.2,7.8:6.6,9.05:8.0,21.8:17.1,24.05:19.2,26.25:21.0,28.5:22.7}[c['y']]
 elif c['group']=='buffers': c['y']=12.7
 elif c['group'].startswith('adc'):
  chipx=5.2 if c['group']=='adc3' else 15.8
  # Reference bypass targets follow the physical package pins.
  refs={17:(3.5,1.8),18:(3.5,-.3),24:(3.5,-2.4),25:(1.2,-3.65),
        26:(1.2,-5.8),29:(-1.3,-3.65),30:(-1.3,-5.8)}
  matched=next((p for p in refs if c['pins'].get('1')==f"U{c['group'][-1]}_{adc_names[p]}"),None)
  if matched is not None:
   dx,dy=refs[matched];c['x'],c['y']=chipx+dx,25.5+dy
  elif c['key']=='C100n': c['x'],c['y'],c['angle']=chipx-3.6,25.5+(0 if c['x']<chipx else 2.1),90
  elif c['key']=='C1u': c['x'],c['y'],c['angle']=chipx+3.6,25.5+(-1.2 if c['x']<chipx else .9),90
  else: c['y']-=6
 elif c['group'] in ['power','unused']: c['y']-=7
 elif c['group']=='midpoint': c['y']-=3.8
 elif c['group']=='digital': c['y']-=8 if c['y']>25 else 3.8
 elif c['group']=='test':
  c['x']=3+5*(int(c['ref'][2:])-1);c['y']=31
  fps[c['ref']].Flip(pos(c['x'],c['y']),False)
 f=fps[c['ref']];f.SetPosition(pos(c['x'],c['y']));f.SetOrientationDegrees(c['angle'])
for ref,x,y in [('C1',2.3,13.5),('C2',12.45,13.5),('C55',11.2,14.5)]:
 c=next(c for c in components if c['ref']==ref);c['x'],c['y'],c['angle']=x,y,90
 fps[ref].SetPosition(pos(x,y));fps[ref].SetOrientationDegrees(90)

# Pack actual courtyard rectangles near functional targets, preserving major ICs.
# Board-edge protrusion is intentional ONLY for the manual angled host connector.
def bounds(f):
 boxes=[g.GetBoundingBox() for g in f.GraphicalItems() if g.GetLayer()==k.F_CrtYd]
 if not boxes: boxes=[p.GetBoundingBox() for p in f.Pads()]
 return (min(b.GetLeft() for b in boxes)/1e6-ORIGIN[0],min(b.GetTop() for b in boxes)/1e6-ORIGIN[1],max(b.GetRight() for b in boxes)/1e6-ORIGIN[0],max(b.GetBottom() for b in boxes)/1e6-ORIGIN[1])
def overlap(a,b,gap=.005):
 return a[0]<b[2]+gap and a[2]+gap>b[0] and a[1]<b[3]+gap and a[3]+gap>b[1]
# Keep the high-impedance input networks together before packing bypass parts.
input_fixed=[]
for c in components:
 if c['group'].startswith('input') and c['key'] in ['R47k','R10M']:
  i=int(c['group'][5:]);cx=2.9+(i-1)*3.8
  if c['key']=='R47k':c['x'],c['y']=cx,5.9
  else:c['x'],c['y']=1.635+1.97*(2*(i-1)+(0 if c['pins']['1'].startswith('HI') else 1)),11.03
  c['angle']=0;f=fps[c['ref']];f.SetPosition(pos(c['x'],c['y']));f.SetOrientationDegrees(0);input_fixed.append(c['ref'])
fixed=['J6','U1','U2','U3','U4','U5','U6']+[c['ref'] for c in components if c['key'] in ['PIEZO','CLAMP']]+input_fixed
occupied=[bounds(fps[ref]) for ref in fixed]
pending=[c for c in components if c['ref'] not in fixed and c['group']!='test']
pending.sort(key=lambda c:(0 if c['group'].startswith('adc') or c['group'] in ['buffers','midpoint'] else 1,-(bounds(fps[c['ref']])[2]-bounds(fps[c['ref']])[0])*(bounds(fps[c['ref']])[3]-bounds(fps[c['ref']])[1])))
for c in pending:
 f=fps[c['ref']]; bx,by=c['x'],c['y']; initial=bounds(f)
 candidates=[]
 region=(.65,4.5,W-.65,32.1)
 for angle in [c['angle'],(c['angle']+90)%180]:
  f.SetOrientationDegrees(angle);initial=bounds(f)
  dx0,dy0,dx1,dy1=initial[0]-bx,initial[1]-by,initial[2]-bx,initial[3]-by
  xx,yy=np.meshgrid(np.arange(.65,W-.65,.10),np.arange(region[1],region[3]+.001,.10))
  xx,yy=xx.ravel(),yy.ravel()
  aa,bb,cc,dd=xx+dx0,yy+dy0,xx+dx1,yy+dy1
  ok=(aa>=region[0])&(bb>=region[1])&(cc<=region[2])&(dd<=region[3])
  for ob in occupied:
   ok &= ~((aa<ob[2]+.005)&(cc+.005>ob[0])&(bb<ob[3]+.005)&(dd+.005>ob[1]))
  valid=np.flatnonzero(ok)
  if len(valid):
   score=(xx[valid]-bx)**2+2*(yy[valid]-by)**2
   ii=valid[np.argmin(score)];cx,cy=float(xx[ii]),float(yy[ii])
   candidates.append((float(np.min(score)),cx,cy,(float(aa[ii]),float(bb[ii]),float(cc[ii]),float(dd[ii])),angle))
 if not candidates: raise RuntimeError('No legal placement for '+c['ref']+' in '+str(region))
 _,cx,cy,r,angle=min(candidates)
 f.SetPosition(pos(cx,cy));f.SetOrientationDegrees(angle)
 c['x']=cx;c['y']=cy;c['angle']=angle;occupied.append(r)
# Final routing clearance adjustments; preserve this placement on regeneration.
for ref,xy in {'C47':(19.35,13.6),'C23':(9.35,26.4),'C24':(9.35,25.2),'C31':(10.85,26.0)}.items():
 c=next(c for c in components if c['ref']==ref);c['x'],c['y']=xy;fps[ref].SetPosition(pos(*xy))
# Omit tiny passive outlines that would be clipped in dense assembly. Package
# outlines remain on F.Fab; all identity is in the assembly drawing/CPL.
for f in fps.values():
 for g in f.GraphicalItems():
  if g.GetLayer()==k.F_SilkS: g.SetLayer(k.F_Fab)
for ref in ['U1','U2','U3','U4','U5','U6']:
 f=fps[ref];p=f.GetPosition();pcbtext(ref,p.x/1e6-ORIGIN[0],p.y/1e6-ORIGIN[1],.8,k.F_Fab)

# Continuous inner reference ground plane. Inner2 remains a routing layer.
z=k.ZONE(board);z.SetLayer(k.In1_Cu);z.SetNet(getnet('GND'));z.SetLocalClearance(mm(.2));z.SetThermalReliefGap(mm(.2));z.SetThermalReliefSpokeWidth(mm(.25));z.SetPadConnection(k.ZONE_CONNECTION_FULL)
poly=z.Outline();poly.NewOutline()
for x,y in [(0,0),(W,0),(W,H),(0,H)]: p=pos(x,y);poly.Append(p.x,p.y)
board.Add(z)
# No unfilled vias in exposed-pad solder paste areas. All other copper allowed.
for ref,padno in [('U1','17'),('U2','17'),('U3','33'),('U4','33')]:
 p=next(p for p in fps[ref].Pads() if p.GetNumber()==padno)
 center=p.GetPosition(); size=p.GetSize();hx=size.x//2+mm(.15);hy=size.y//2+mm(.15)
 keep=k.ZONE(board);keep.SetIsRuleArea(True);keep.SetLayer(k.F_Cu)
 keep.SetDoNotAllowVias(True);keep.SetDoNotAllowTracks(False)
 keep.SetDoNotAllowPads(False);keep.SetDoNotAllowCopperPour(False);keep.SetDoNotAllowFootprints(False)
 outline=keep.Outline();outline.NewOutline()
 for dx,dy in [(-hx,-hy),(hx,-hy),(hx,hy),(-hx,hy)]:outline.Append(center.x+dx,center.y+dy)
 board.Add(keep)

# U4 interrupt is unused by Hat2. Omit its pull-up and leave output NC.
removed_pullup=fps.pop('R27');board.Remove(removed_pullup)
components[:]=[c for c in components if c['ref']!='R27']
next(c for c in components if c['ref']=='U4')['pins']['13']=None
next(p for p in fps['U4'].Pads() if p.GetNumber()=='13').SetNetCode(0)

project=json.loads(Path('/usr/share/kicad/template/kicad.kicad_pro').read_text())
project['meta']['filename']=NAME+'.kicad_pro'
ds.SetAuxOrigin(pos(0,H))
project['board']['design_settings']['rules']={'min_clearance':.1,'min_track_width':.1,'min_copper_edge_clearance':.5,'min_hole_clearance':.2,'min_via_diameter':.4,'min_through_hole_diameter':.2,'min_via_annular_width':.1,'min_silk_clearance':.15,'min_text_height':1.0,'min_text_thickness':.15,'min_courtyard_clearance':.0}
project['net_settings']={'meta':{'version':4},'classes':[{'name':'Default','priority':2147483647,'description':'Default 0.1mm clearance / 0.4-0.2mm vias','clearance':.1,'track_width':.127,'via_diameter':.4,'via_drill':.2,'microvia_diameter':.2,'microvia_drill':.1,'diff_pair_width':.15,'diff_pair_gap':.2,'diff_pair_via_gap':.25,'bus_width':12,'wire_width':6,'line_style':0,'schematic_color':'rgba(0, 0, 0, 0.000)','pcb_color':'rgba(0, 0, 0, 0.000)'}],'netclass_assignments':{},'netclass_patterns':[]}
# SaveBoard can persist a dummy project's defaults. Write rules AFTER it.
k.SaveBoard(str(OUT/(NAME+'-placement.kicad_pcb')),board)
(OUT/(NAME+'.kicad_pro')).write_text(json.dumps(project,indent=2))
(OUT/(NAME+'-placement.kicad_pro')).write_text(json.dumps(project,indent=2))

# Full pin-to-net schematic. Functional groups use local net labels; no hidden pins.
# Device block symbols deliberately show actual package pin numbers for review.
symbols={}
pinlocations={}
def make_symbol(key,pinnames,types=None):
 types=types or {}; count=len(pinnames); nleft=math.ceil(count/2)
 width=6.35 if count<=3 else 12.7
 step=2.54; height=max(5.08,nleft*step)
 lines=[f'(symbol "PiezoHat:{key}" (pin_names (offset 0.8)) (in_bom yes) (on_board yes)',f'(property "Reference" "X" (at 0 {height/2+2} 0) (effects (font (size 1.27 1.27))))',f'(property "Value" "{key}" (at 0 {-height/2-2} 0) (effects (font (size 1 1))))',f'(symbol "{key}_0_1" (rectangle (start {-width} {height/2}) (end {width} {-height/2}) (stroke (width .254) (type default)) (fill (type background))))',f'(symbol "{key}_1_1"']
 loc={}
 for idx,(number,name) in enumerate(pinnames.items()):
  left=idx<nleft;row=idx if left else idx-nleft
  x=-width-2.54 if left else width+2.54;y=height/2-1.27-row*step
  angle=0 if left else 180
  lines.append(f'(pin {types.get(number,"passive")} line (at {x} {y} {angle}) (length 2.54) (name {q(name)} (effects (font (size .8 .8)))) (number {q(number)} (effects (font (size .8 .8)))))')
  loc[number]=(x,y,left)
 lines.extend(['))'])
 symbols[key]='\n'.join(lines);pinlocations[key]=loc
make_symbol('R',{'1':'1','2':'2'})
make_symbol('C',{'1':'1','2':'2'})
make_symbol('CLAMP',{'1':'A1/GND','2':'K2/V+','3':'K1/A2/IN'})
make_symbol('PIEZO',{'1':'SIG','2':'GND'})
make_symbol('TP',{'1':'TEST'})
make_symbol('OP',{str(n):s for n,s in opnames.items()},{str(n):'output' if s.startswith('OUT') else 'power_in' if n in [2,11,17] else 'no_connect' if s=='NC' else 'input' for n,s in opnames.items()})
make_symbol('ADC',{str(n):s for n,s in adc_names.items()},{str(n):'power_in' if n in [6,7,8,21,22,23,33] else 'input' if n in [1,2,4,5,9,10,12,15,16,19,20,27,28,31,32] else 'bidirectional' if n==3 else 'open_collector' if n==13 else 'output' if n in [11,14] else 'passive' for n in adc_names})
make_symbol('LDO',{'1':'VIN','2':'GND','3':'EN','4':'NC','5':'VOUT'},{'1':'power_in','2':'power_in','3':'input','4':'no_connect','5':'power_out'})
make_symbol('HAT',{str(n):s for n,s in enumerate(['GND','G5/SCL','EXT_5V','G4/SDA','BOOT','G6/WS','G1/MCLK','G7/BCLK','G8/DATA','G43','BAT','G44','3V3_L2','G2','5VIN','G3'],1)},{'1':'power_out','2':'output','3':'power_out','4':'bidirectional','6':'output','7':'output','8':'output','9':'input'})
sch=[f'(kicad_sch (version 20250114) (generator "eeschema") (uuid {ROOT}) (paper "A2") (title_block (title "StickS3 Piezo Hat R1 - 5 inputs / single-sided") (date "2026-10-06") (rev "R1 compact prototype") (comment 1 "Pin-to-net view; read README for host numbering and assembly"))','(lib_symbols',*symbols.values(),')']
def schtext(t,x,y,size=1.3):
 sch.append(f'(text {q(t)} (at {x} {y} 0) (effects (font (size {size} {size})) (justify left bottom)) (uuid {uid(t+str(x)+str(y))}))')
def schpart(c,x,y):
 x=round(x/1.27)*1.27;y=round(y/1.27)*1.27
 key=c['key'] if c['key'] in symbols else c['ref'][0]
 if key not in symbols: key='R' if c['ref'][0]=='R' else 'C'
 ref=c['ref'];h=max(5.08,math.ceil(len(pinlocations[key])/2)*2.54)
 sch.append(f'(symbol (lib_id "PiezoHat:{key}") (at {x} {y} 0) (unit 1) (in_bom {"no" if c["assembly"]=="NONE" else "yes"}) (on_board yes) (dnp no) (uuid {c["uuid"]}) (property "Reference" {q(ref)} (at {x} {y-h/2-2.5} 0) (effects (font (size 1.2 1.2)))) (property "Value" {q(c["value"])} (at {x} {y+h/2+2.5} 0) (effects (font (size .9 .9)))) (property "Footprint" {q(c["footprint"])} (at {x} {y} 0) (effects (font (size 1 1)) hide)) (property "LCSC" {q(c["lcsc"])} (at {x} {y} 0) (effects (font (size 1 1)) hide)) (property "Assembly" {q(c["assembly"])} (at {x} {y} 0) (effects (font (size 1 1)) hide)) (instances (project "{NAME}" (path "/{ROOT}" (reference {q(ref)}) (unit 1)))))')
 for n,(px,py,left) in pinlocations[key].items():
  ex,ey=x+px,y-py;v=c['pins'].get(n)
  if v is None:
   sch.append(f'(no_connect (at {ex} {ey}) (uuid {uid(ref+"NC"+n)}))')
  else:
   lx=ex+(-2.54 if left else 2.54)
   sch.append(f'(wire (pts (xy {ex} {ey}) (xy {lx} {ey})) (stroke (width 0) (type default)) (uuid {uid(ref+"wire"+n)}))')
   sch.append(f'(label {q(v)} (at {lx} {ey} {180 if left else 0}) (effects (font (size .8 .8)) (justify {"right" if left else "left"} bottom)) (uuid {uid(ref+"label"+n)}))')

for i in range(1,6):
 cs=[c for c in components if c['group']=='input'+str(i)]
 schtext(f'STRING {i}: 20M load / unity buffer / passive two-pole LPF / AC isolated ADC',16,17+(i-1)*35,1.2)
 for j,c in enumerate(cs): schpart(c,34+(j%8)*60,29+(i-1)*35+(j//8)*17)
schtext('BUFFERS: U1 A-D = strings 1-4; U2 A = string 5; U2 B = VMID; C/D terminated',16,207)
for c,x,y in [(next(c for c in components if c['ref']=='U1'),65,226),(next(c for c in components if c['ref']=='U2'),150,226),(next(c for c in components if c['ref']=='U3'),260,245),(next(c for c in components if c['ref']=='U4'),350,245),(next(c for c in components if c['ref']=='J6'),470,230),(next(c for c in components if c['ref']=='U5'),60,265),(next(c for c in components if c['ref']=='U6'),150,265)]: schpart(c,x,y)
used={c['ref'] for c in components if c['group'].startswith('input') or c['key'] in ['OP','ADC','HAT','LDO']}
remaining=[c for c in components if c['ref'] not in used]
schtext('DECOUPLING / BIAS / CLOCK TERMINATION / TEST: all pins and nets explicit',16,289)
for j,c in enumerate(remaining): schpart(c,34+(j%9)*60,304+(j//9)*17)
schtext('ADC3 = address 0x40 / inputs 1-4; ADC4 = 0x41 / input 5 plus unused inputs 6-8',16,405,1)
sch.append('(sheet_instances (path "/" (page "1"))))')
(OUT/(NAME+'.kicad_sch')).write_text('\n'.join(sch))
(OUT/'PiezoHat.kicad_sym').write_text('(kicad_symbol_lib (version 20241209) (generator "kicad_symbol_editor")\n'+'\n'.join(s.replace('"PiezoHat:', '"') for s in symbols.values())+'\n)\n')
(OUT/'sym-lib-table').write_text('(sym_lib_table (version 7) (lib (name "PiezoHat") (type "KiCad") (uri "${KIPRJMOD}/PiezoHat.kicad_sym") (options "") (descr "Project pin-explicit symbols")))\n')
(OUT/'design.json').write_text(json.dumps({'board':{'width':W,'length':H,'corner_radius':R,'layers':4,'thickness':1},'components':components},ensure_ascii=False,indent=2))

for fn,include in [('BOM_JLCPCB.csv',lambda c:c['assembly']=='JLC'),('BOM_full.csv',lambda c:c['assembly']!='NONE')]:
 groups=defaultdict(list)
 for c in components:
  if include(c): groups[(c['value'],c['footprint'],c['lcsc'],c['assembly'],c['mpn'])].append(c['ref'])
 with (OUT/fn).open('w',newline='') as f:
  w=csv.writer(f);w.writerow(['Comment','Designator','Footprint','LCSC Part #'] if fn=='BOM_JLCPCB.csv' else ['Comment','Designator','Footprint','LCSC Part #','Quantity','Assembly','Manufacturer Part'])
  for (val,foot,code,asm,mpn),refs in groups.items():
   row=[val,','.join(refs),foot,code]
   w.writerow(row if fn=='BOM_JLCPCB.csv' else row+[len(refs),asm,mpn])
with (OUT/'HAND_ASSEMBLY.csv').open('w',newline='') as f:
 w=csv.writer(f);w.writerow(['Designator','Part','LCSC','Instructions'])
 for c in components:
  if c['assembly']=='HAND':w.writerow([c['ref'],c['mpn'],c['lcsc'],'Fit after PCBA; required before powered piezo use'])
print(f'Created {len(components)} footprints / {len(net)} named nets / {W}x{H}mm R{R}, all component bodies on front')
