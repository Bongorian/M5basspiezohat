#!/usr/bin/python3
"""Remove only dangling copper whose removal preserves pad connectivity."""
import json,subprocess,sys
from pathlib import Path
src=Path(sys.argv[1]).resolve();report=Path(sys.argv[2]).resolve();root=src.parent
for i in range(60):
 r=json.loads(report.read_text());print('TRIM',i,len(r['unconnected_items']),len(r['violations']),flush=True)
 assert not r['unconnected_items'],'Do not trim an incomplete board'
 if not r['violations']:break
 assert all(v['type'] in ['track_dangling','via_dangling'] for v in r['violations']),r['violations']
 dst=root/f'compact-final-clean-{i}.kicad_pcb'
 subprocess.run(['/usr/bin/python3',str(Path(__file__).resolve().parent/'trim_dangling.py'),str(src),str(report),str(dst)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,check=True)
 report=dst.with_suffix('.drc.json');subprocess.run(['kicad-cli','pcb','drc','--format','json','-o',str(report),str(dst)],stdout=subprocess.DEVNULL,check=True);src=dst
else:raise RuntimeError('Dangling cleanup did not converge')
(root/'clean-candidate.json').write_text(json.dumps({'board':str(src),'drc':str(report)},indent=2));print('CLEAN',src,flush=True)
