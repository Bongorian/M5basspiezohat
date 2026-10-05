#!/usr/bin/python3
"""Local route with the project's real net rules; never overwrite the source.
KiCad 9 LoadBoard does not automatically apply project net class sizes to DSN.
"""
import argparse,json,subprocess,shutil
from pathlib import Path
import pcbnew as k
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=Path);p.add_argument('output',type=Path)
p.add_argument('--passes',type=int,default=12);p.add_argument('--fanout-passes',type=int,default=3)
a=p.parse_args();src=a.source.resolve();dst=a.output.resolve()
if dst.exists() or src==dst:p.error('output must be a new path')
work=dst.parent/(dst.stem+'-routing');work.mkdir(exist_ok=False)
b=k.LoadBoard(str(src));pro=src.with_suffix('.kicad_pro')
for zone in b.Zones():
 if not zone.GetIsRuleArea():zone.UnFill()
rules=json.loads(pro.read_text())['net_settings']['classes']
assert len(rules)==1 and rules[0]['name']=='Default','Extend this exporter before adding net classes'
r=rules[0];ds=b.GetDesignSettings();ns=ds.m_NetSettings;nc=ns.GetDefaultNetclass()
for setter,key in [('SetTrackWidth','track_width'),('SetClearance','clearance'),('SetViaDiameter','via_diameter'),('SetViaDrill','via_drill')]:getattr(nc,setter)(k.FromMM(r[key]))
dsn=work/'input.dsn';ses=work/'output.ses'
assert k.ExportSpecctraDSN(b,str(dsn))
text=dsn.read_text();assert f"(width {round(r['track_width']*1000)})" in text and f"{round(r['via_diameter']*1000)}:{round(r['via_drill']*1000)}_um" in text
text=text.replace('(layer In1.Cu\n      (type signal)','(layer In1.Cu\n      (type power)').replace('(clearance 31.75 (type smd_smd))','(clearance 150 (type smd_smd))')
dsn.write_text(text)
with (work/'router.log').open('w') as log:
 subprocess.run(['freerouting-cli','-de',str(dsn),'-do',str(ses),'-mp',str(a.passes),'--router.optimizer.enabled=false','--router.automatic_neckdown=false','--router.neck_width_um=100','--router.fanout.max_passes='+str(a.fanout_passes),'--router.copperToEdgeClearanceUm=500'],stdout=log,stderr=subprocess.STDOUT,check=True)
assert k.ImportSpecctraSES(b,str(ses));k.ZONE_FILLER(b).Fill(b.Zones())
project_text=pro.read_text();k.SaveBoard(str(dst),b);dst.with_suffix('.kicad_pro').write_text(project_text)
subprocess.run(['kicad-cli','pcb','drc','--format','json','--exit-code-violations','-o',str(work/'drc.json'),str(dst)],check=True)
print('Routed board:',dst)
