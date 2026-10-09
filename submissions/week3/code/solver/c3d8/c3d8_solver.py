"""Strict production process wrapper for full-integration structured C3D8 solids."""
import argparse, hashlib, json, math, os, signal, sys, tempfile, traceback
from pathlib import Path

CONTRACT_VERSION=1
NUMERICAL_ACCEPTANCE_LIMITS={
    'scaledResidual':1e-10,
    'symmetryRelative':1e-12,
    'forceBalanceRelative':1e-9,
    'momentBalanceRelative':1e-9,
}
def enforce_numerical_acceptance(summary):
    checks=summary.get('checks') if isinstance(summary,dict) else None
    if not isinstance(checks,dict): raise RuntimeError('Numerical acceptance failed: missing checks')
    failures=[]
    for name,limit in NUMERICAL_ACCEPTANCE_LIMITS.items():
        value=checks.get(name)
        if type(value) not in (int,float) or not math.isfinite(value) or value<0 or value>limit:
            failures.append(f'{name}={value!r} (limit {limit:g})')
    if failures: raise RuntimeError('Numerical acceptance failed: '+', '.join(failures))
    summary['numericalAcceptance']={'status':'passed','limits':dict(NUMERICAL_ACCEPTANCE_LIMITS)}
    return summary

def unique_object(pairs):
    out={}
    for key,value in pairs:
        if key in out: raise ValueError(f'Duplicate JSON key: {key}')
        out[key]=value
    return out

def exact(value,keys,name):
    if not isinstance(value,dict) or set(value)!=set(keys): raise ValueError(f'{name} must contain exactly: {", ".join(keys)}')

def number(value,name):
    if type(value) not in (int,float) or not math.isfinite(value): raise ValueError(f'{name} must be a finite number')
    return float(value)

def validate_input(data):
    exact(data,('nodes','material','subgrade','loads','options'),'input')
    exact(data['nodes'],('x','y','z'),'nodes')
    for axis in 'xyz':
        values=data['nodes'][axis]
        if not isinstance(values,list) or len(values)<2: raise ValueError(f'nodes.{axis} requires at least two entries')
        values[:]=[number(v,f'nodes.{axis}') for v in values]
        if any(b<=a or not math.isfinite(b-a) for a,b in zip(values,values[1:])): raise ValueError(f'nodes.{axis} must be strictly increasing')
    exact(data['material'],('Emod','nu'),'material'); E=number(data['material']['Emod'],'material.Emod'); nu=number(data['material']['nu'],'material.nu')
    if E<=0 or not -1<nu<.5: raise ValueError('Require Emod > 0 and -1 < nu < 0.5')
    exact(data['subgrade'],('Kx','Ky','Kz'),'subgrade')
    for key in ('Kx','Ky','Kz'):
        if number(data['subgrade'][key],f'subgrade.{key}')<=0: raise ValueError(f'subgrade.{key} must be greater than zero')
    exact(data['options'],('reduced_integration',),'options')
    if type(data['options']['reduced_integration']) is not bool: raise ValueError('options.reduced_integration must be boolean')
    if data['options']['reduced_integration']: raise ValueError('Reduced integration is unsupported')
    if not isinstance(data['loads'],list): raise ValueError('loads must be an array')
    x,y=data['nodes']['x'],data['nodes']['y']
    for i,load in enumerate(data['loads']):
        exact(load,('q','x1','x2','y1','y2'),f'loads[{i}]')
        for key in ('q','x1','x2','y1','y2'): load[key]=number(load[key],f'loads[{i}].{key}')
        if not (load['q']>0 and x[0]<=load['x1']<load['x2']<=x[-1] and y[0]<=load['y1']<load['y2']<=y[-1]):
            raise ValueError(f'loads[{i}] requires positive q, positive area and bounds inside X/Y')
    return data

def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def atomic_json(path,data):
    temp=Path(str(path)+'.tmp')
    with temp.open('w',encoding='utf-8') as stream:
        json.dump(data,stream,indent=2,allow_nan=False); stream.flush(); os.fsync(stream.fileno())
    os.replace(temp,path)

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('--input',type=Path); parser.add_argument('--output-dir',type=Path)
    parser.add_argument('--check-backend',action='store_true'); parser.add_argument('--events-jsonl',action='store_true'); parser.add_argument('--diagnostics',choices=('none','summary','full'),default='summary'); parser.add_argument('--cancel-file',type=Path)
    args=parser.parse_args(argv)
    if args.check_backend:
        try:
            import numpy,scipy
            from c3d8_analysis import run_analysis
            print(f'Backend imports OK: Python {sys.version.split()[0]}, NumPy {numpy.__version__}, SciPy {scipy.__version__}')
            return 0
        except ImportError as error: print(error,file=sys.stderr); return 3
    if args.input is None or args.output_dir is None: parser.error('--input and --output-dir are required')
    def emit(event):
        if args.events_jsonl: print(json.dumps({'contractVersion':CONTRACT_VERSION,**event},allow_nan=False),flush=True)
    cancelled=False
    def request_cancel(*_):
        nonlocal cancelled; cancelled=True
    for sig in (signal.SIGINT,signal.SIGTERM): signal.signal(sig,request_cancel)
    def cancellation_requested(): return cancelled or (args.cancel_file is not None and args.cancel_file.exists())
    stage=None
    try:
        emit({'type':'phase','phase':'validation','progressKnown':False})
        if not args.input.is_absolute() or not args.output_dir.is_absolute(): raise ValueError('input and output paths must be absolute')
        raw=args.input.read_bytes(); data=validate_input(json.loads(raw.decode('utf-8-sig'),object_pairs_hook=unique_object))
        if cancellation_requested(): emit({'type':'cancelled','message':'Cancelled by user.'}); return 130
        if args.output_dir.exists(): raise OSError('Output directory already exists')
        args.output_dir.parent.mkdir(parents=True,exist_ok=True); stage=Path(tempfile.mkdtemp(prefix=args.output_dir.name+'.partial-',dir=args.output_dir.parent)); (stage/'input.json').write_bytes(raw)
        from c3d8_analysis import AnalysisCancelled,run_analysis
        import numpy,scipy
        try:
            summary=run_analysis(data,stage,emit,cancellation_requested)
            summary=enforce_numerical_acceptance(summary)
        except AnalysisCancelled: emit({'type':'cancelled','message':'Cancelled by user.'}); return 130
        result=stage/'results.vtk'
        manifest={'contractVersion':CONTRACT_VERSION,'status':'Completed','result':'results.vtk','inputSha256':hashlib.sha256(raw).hexdigest(),'resultSha256':digest(result),'unitSystem':'mm-N-MPa','backendId':'c3d8-python-v1','modelKind':'structured_solid_c3d8','summary':summary,'runtime':{'python':sys.version.split()[0],'numpy':numpy.__version__,'scipy':scipy.__version__},'sources':{name:digest(Path(__file__).parent/name) for name in ('c3d8_solver.py','c3d8_analysis.py','C3D8local.py')}}
        atomic_json(stage/'run-result.json',manifest)
        if cancellation_requested(): emit({'type':'cancelled','message':'Cancelled by user.'}); return 130
        os.rename(stage,args.output_dir); emit({'type':'completed','manifest':'run-result.json'}); return 0
    except ValueError as error: emit({'type':'error','code':'INVALID_INPUT','message':str(error)}); return 2
    except ImportError as error: emit({'type':'error','code':'MISSING_DEPENDENCY','message':str(error)}); return 3
    except (ArithmeticError,RuntimeError) as error: emit({'type':'error','code':'NUMERICAL_FAILURE','message':str(error)}); return 4
    except OSError as error: emit({'type':'error','code':'EXPORT_FAILURE','message':str(error)}); return 5
    except Exception as error: emit({'type':'error','code':'INTERNAL_ERROR','message':str(error)}); traceback.print_exc(file=sys.stderr); return 1
    finally:
        if stage and stage.exists():
            import shutil; shutil.rmtree(stage,ignore_errors=True)

if __name__=='__main__': sys.exit(main())
