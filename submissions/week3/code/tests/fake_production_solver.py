"""Test-only JSONL backend, copied as plate_solver.py to exercise the contract."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import time

parser = argparse.ArgumentParser()
parser.add_argument('--input', type=Path)
parser.add_argument('--output-dir', type=Path)
parser.add_argument('--cancel-file', type=Path)
parser.add_argument('--events-jsonl', action='store_true')
parser.add_argument('--diagnostics')
args = parser.parse_args()
spec = json.loads(args.input.read_text(encoding='utf-8'))
scenario = spec['scenario']

def event(type, **fields):
    print(json.dumps(dict(contractVersion=1, type=type, **fields)), flush=True)

event('phase', phase='assembly', completed=0, total=4)
if scenario == 'hang':
    while not args.cancel_file.exists():
        time.sleep(0.02)
    event('cancelled', message='Cancelled cooperatively.')
    sys.exit(130)
if scenario == 'malformed':
    print('not JSON', flush=True)
if scenario == 'error':
    event('error', message='Backend reported failure.')
if scenario == 'exit7':
    event('error', message='Controlled numerical failure.')
    sys.exit(7)
args.output_dir.mkdir()
shutil.copyfile(spec['fixture'], args.output_dir / 'results.vtk')
manifest = dict(contractVersion=1, status='Completed', unitSystem='mm-N-MPa', result='results.vtk',
                inputSha256=hashlib.sha256(args.input.read_bytes()).hexdigest(),
                resultSha256=hashlib.sha256((args.output_dir / 'results.vtk').read_bytes()).hexdigest())
if spec.get('backendId'):
    checks = dict(scaledResidual=1e-14, symmetryRelative=0.0,
                  forceBalanceRelative=1e-14, momentBalanceRelative=1e-14)
    if scenario == 'unhealthy_checks':
        checks['forceBalanceRelative'] = 1e-2
    manifest.update(backendId=spec['backendId'], modelKind=spec['modelKind'],
                    summary=dict(checks=checks, numericalAcceptance=dict(status='passed')))
    if scenario == 'missing_acceptance':
        del manifest['summary']['numericalAcceptance']
if scenario == 'wrong_input':
    manifest['inputSha256'] = 'wrong'
if scenario == 'wrong_result':
    manifest['resultSha256'] = 'wrong'
if scenario != 'missing_manifest':
    (args.output_dir / 'run-result.json').write_text(json.dumps(manifest), encoding='utf-8')
if scenario != 'missing_completion':
    event('completed', manifest='run-result.json')
if scenario == 'duplicate_completion':
    event('completed', manifest='run-result.json')
