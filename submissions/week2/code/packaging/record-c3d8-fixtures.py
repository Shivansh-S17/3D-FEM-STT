"""Publish a successful capture into a fresh, versionable fixture directory."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('report_directory',type=Path)
args=parser.parse_args()
source=args.report_directory.resolve()
report=json.loads((source/'report.json').read_text(encoding='utf-8'))
if report.get('status')!='Passed':
    raise SystemExit('Only a fully passed report may become a reference fixture.')
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
for name,checksum in report['sourceHashes'].items():
    if Path(name).name!=name or digest(ROOT/'C3D8v/python'/name)!=checksum:
        raise SystemExit('Received source changed since capture: '+name)
for row in report['runs']:
    name=row['case']
    if Path(name).name!=name: raise SystemExit('Invalid case directory')
    folder=source/name
    for path,key in [(folder/'input.json','inputSha256'),(folder/'direct/output-c3d8.csv','csvSha256'),
                     (folder/'capture/arrays.npz','arraysSha256')]:
        if digest(path)!=row[key]: raise SystemExit('Captured artifact hash mismatch: '+str(path))
destination=ROOT/'tests/fixtures/c3d8'
destination.mkdir(parents=True,exist_ok=False)
for row in report['runs']:
    name=row['case']; folder=source/name; target=destination/name
    expected=target/'expected'; expected.mkdir(parents=True)
    shutil.copy2(folder/'input.json',target/'input.json')
    shutil.copy2(folder/'direct/output-c3d8.csv',expected/'reference.csv')
    shutil.copy2(folder/'capture/arrays.npz',expected/'arrays.npz')
    shutil.copy2(folder/'capture/capture.json',expected/'checks.json')
    shutil.copy2(folder/'direct/stdout.log',expected/'stdout.log')
    shutil.copy2(folder/'direct/stderr.log',expected/'stderr.log')
    (expected/'provenance.json').write_text(json.dumps({**row,'sourceHashes':report['sourceHashes'],
        'python':report['python'],'dependencies':report['dependencies']},indent=2),encoding='utf-8')
shutil.copy2(source/'report.json',ROOT/'docs/C3D8_BASELINE_MEASUREMENTS.json')
print('Published',len(report['runs']),'reference cases to',destination)
