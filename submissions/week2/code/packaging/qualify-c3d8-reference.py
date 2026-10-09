"""Capture unmodified C3D8 runs and raw arrays; all publication is explicit."""
import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import runpy
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'C3D8v/python'
sys.path.insert(0, str(ROOT / 'tests'))
from c3d8_cases import cases


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def capture(input_path, output):
    import numpy as np
    sys.path.insert(0, str(SOURCE))
    sys.argv = [str(SOURCE / 'C3D8-slab.py'), str(input_path)]
    # Execute the received __main__ block, including its real CSV export. No
    # imports/functions/math are mocked or patched. Inspect returned globals.
    state = runpy.run_path(str(SOURCE / 'C3D8-slab.py'), run_name='__main__')
    mesh, U, F, K = state['mesh'], state['Ug'], state['Fg'], state['Kg'].tocsr()
    strain, stress = state['post_processor'](mesh, state['slab_properties'], U)
    coordinates = mesh.getAllNodeCoordinates()
    arrays = dict(coordinates=coordinates, displacement=U.reshape(-1,3),
                  strain=strain, stress=stress, force=F.reshape(-1,3),
                  connectivity=np.array([mesh.getNodesOfElement(e) for e in range(mesh.getNumElements())]))
    for name, values in arrays.items():
        if not np.isfinite(values).all():
            raise ValueError('Nonfinite reference array: ' + name)
    np.savez_compressed(output / 'arrays.npz', **arrays)
    residual = K@U-F
    denominator = np.abs(K)@np.abs(U)+np.abs(F)
    scaled = [float(np.max(np.abs(residual[d::3]))/max(np.max(denominator[d::3]),np.finfo(float).tiny)) for d in range(3)]
    difference = K-K.T
    symmetry = float(np.max(np.abs(difference.data))/np.max(np.abs(K.data))) if difference.nnz else 0.
    data = json.loads(input_path.read_text())
    expected = np.array([0.,0.,sum(p['q']*(p['x2']-p['x1'])*(p['y2']-p['y1']) for p in data['loads'])])
    # Independent bilinear-face integration of spring traction, not Kslocal.
    reaction = np.zeros_like(arrays['displacement'])
    gauss = (-1/np.sqrt(3),1/np.sqrt(3))
    coefficients = np.array([data['subgrade'][k] for k in ('Kx','Ky','Kz')])
    for e in mesh.getBottomElements():
        ids=mesh.getNodesOfElement(e)[4:]; a,b,_=mesh.getSizeOfElement(e)
        for xi in gauss:
            for eta in gauss:
                N=np.array([(1-xi)*(1-eta),(1+xi)*(1-eta),(1+xi)*(1+eta),(1-xi)*(1+eta)])/4
                traction=-(N@arrays['displacement'][ids])*coefficients
                reaction[ids] += N[:,None]*traction*a*b/4
    balance = reaction.sum(axis=0)+F.reshape(-1,3).sum(axis=0)
    moments = np.cross(coordinates,reaction+F.reshape(-1,3)).sum(axis=0)
    scale=max(1.,float(np.linalg.norm(expected)))
    length=max(1.,float(np.max(np.ptp(coordinates,axis=0))))
    checks=dict(scaledResidualXYZ=scaled,symmetryRelative=symmetry,
                appliedForceXYZ=F.reshape(-1,3).sum(axis=0).tolist(),expectedForceXYZ=expected.tolist(),
                springReactionXYZ=reaction.sum(axis=0).tolist(),forceBalanceRelative=float(np.linalg.norm(balance)/scale),
                momentBalanceRelative=float(np.linalg.norm(moments)/(scale*length)))
    if max(scaled)>1e-10 or symmetry>1e-12 or checks['forceBalanceRelative']>1e-8 or checks['momentBalanceRelative']>1e-8:
        raise ValueError('Reference equilibrium checks failed: '+json.dumps(checks))
    if not np.allclose(F.reshape(-1,3).sum(axis=0),expected,atol=1e-8,rtol=1e-11):
        raise ValueError('Wrong assembled force resultant')
    summary=dict(nodes=mesh.getNumNodes(),elements=mesh.getNumElements(),dofs=mesh.getNumDofs(),
                 stiffnessNonzeros=K.nnz,checks=checks,
                 displacementMin=arrays['displacement'].min(axis=0).tolist(),displacementMax=arrays['displacement'].max(axis=0).tolist(),
                 stressMin=stress.min(axis=0).tolist(),stressMax=stress.max(axis=0).tolist())
    (output/'capture.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')


def bounded_run(command, folder):
    import psutil
    start=time.perf_counter(); peak=0; stopped=None
    with (folder/'stdout.log').open('wb') as stdout, (folder/'stderr.log').open('wb') as stderr:
        child=subprocess.Popen(command,cwd=folder,stdout=stdout,stderr=stderr,
                               env={**os.environ,'PYTHONUTF8':'1','PYTHONDONTWRITEBYTECODE':'1','MPLBACKEND':'Agg',
                                    'MPLCONFIGDIR':str(folder/'matplotlib-cache')})
        process=psutil.Process(child.pid)
        while child.poll() is None:
            try:
                descendants=process.children(recursive=True)
                usage=0
                for member in [process,*descendants]:
                    try:
                        info=member.memory_info(); usage+=getattr(info,'peak_wset',info.rss)
                    except psutil.NoSuchProcess:
                        pass
                peak=max(peak,usage)
            except psutil.NoSuchProcess:
                break
            if time.perf_counter()-start>120 or peak>1536*1024**2:
                stopped='120-second / 1536-MiB resource guard'
                for member in reversed(descendants):
                    try: member.kill()
                    except psutil.NoSuchProcess: pass
                child.kill(); break
            time.sleep(.02)
        code=child.wait(timeout=5)
    result=dict(exitCode=code,wallSeconds=time.perf_counter()-start,peakWorkingSetBytes=peak)
    if stopped or code:
        raise RuntimeError(f'Reference process failed: {stopped or code}; see {folder}/stderr.log')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    if args.capture:
        capture(args.capture.resolve(),Path.cwd()); return
    if args.output is None:
        parser.error('--output requires a fresh report directory')
    output=args.output.resolve()
    output.mkdir(parents=True,exist_ok=False)
    import numpy as np
    dependencies={d.metadata['Name']:d.version for d in importlib.metadata.distributions()}
    report=dict(status='Running',purpose='Unmodified C3D8 software baseline; author/independent benchmark acceptance remains open',
                python=platform.python_version(),platform=platform.platform(),dependencies=dependencies,
                memoryMeasurement='20 ms polling; maximum observed sum of per-process peak working sets across owned launcher/solver tree (RSS fallback)',
                sourceHashes={p.name:digest(p) for p in SOURCE.glob('*.py')},
                rankAndConventionAudit='docs/C3D8_INTAKE_AUDIT.json',runs=[])
    for name,data in cases().items():
        folder=output/name; folder.mkdir()
        path=folder/'input.json'; path.write_text(json.dumps(data,indent=2),encoding='utf-8')
        direct=folder/'direct'; direct.mkdir()
        direct_measure=bounded_run([sys.executable,'-B',str(SOURCE/'C3D8-slab.py'),str(path)],direct)
        inspected=folder/'capture'; inspected.mkdir()
        captured_measure=bounded_run([sys.executable,'-B',str(Path(__file__).resolve()),'--capture',str(path)],inspected)
        left=np.genfromtxt(direct/'output-c3d8.csv',delimiter=',',names=True)
        right=np.genfromtxt(inspected/'output-c3d8.csv',delimiter=',',names=True)
        if left.dtype.names!=right.dtype.names or left.shape!=right.shape:
            raise ValueError('Direct/captured CSV schema differs')
        for field in left.dtype.names:
            np.testing.assert_allclose(left[field],right[field],rtol=1e-12,atol=1e-12)
        with np.load(inspected/'arrays.npz') as arrays:
            if name=='analytical_compression':
                z=arrays['coordinates'][:,2]; q=data['loads'][0]['q']; E=data['material']['Emod']; kz=data['subgrade']['Kz']
                expected_w=q/kz+q*(max(z)-z)/E
                np.testing.assert_allclose(arrays['displacement'][:,2],expected_w,atol=1e-10,rtol=1e-9)
                np.testing.assert_allclose(arrays['displacement'][:,:2],0.,atol=1e-10)
                expected_stress=np.zeros_like(arrays['stress']); expected_stress[:,2]=-q
                expected_strain=np.zeros_like(arrays['strain']); expected_strain[:,2]=-q/E
                np.testing.assert_allclose(arrays['stress'],expected_stress,atol=1e-8)
                np.testing.assert_allclose(arrays['strain'],expected_strain,atol=1e-12)
            if name=='zero_load':
                for field in ('displacement','strain','stress','force'):
                    np.testing.assert_array_equal(arrays[field],np.zeros_like(arrays[field]))
        row=dict(case=name,direct=direct_measure,capture=captured_measure,
                 inputSha256=digest(path),csvSha256=digest(direct/'output-c3d8.csv'),
                 arraysSha256=digest(inspected/'arrays.npz'),summary=json.loads((inspected/'capture.json').read_text()))
        report['runs'].append(row)
        (output/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        print(f'{name}: direct {direct_measure["wallSeconds"]:.2f}s, {direct_measure["peakWorkingSetBytes"]/1024**2:.1f} MiB; checks passed',flush=True)
    report['status']='Passed'
    (output/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')


if __name__=='__main__':
    main()
