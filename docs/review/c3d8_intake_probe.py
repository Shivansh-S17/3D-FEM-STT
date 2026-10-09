"""Read-only intake probes; does not execute or modify the supplied driver."""
import ast
import hashlib
import json
from pathlib import Path
import sys
import importlib.util
import subprocess
import numpy as np
from abc import ABC, abstractmethod

root = Path(__file__).resolve().parents[2]
source = root / 'C3D8v/python'
sys.path.insert(0, str(source))
import C3D8local as local

report = {'scope': 'Source review and bounded local probes, not full driver or GetFEM validation',
          'files': {}, 'elements': [], 'meshes': []}
report['runtime'] = {'python': sys.version.split()[0], 'numpy': np.__version__}
report['rankRelativeThreshold'] = 1e-10
report['dependenciesAvailable'] = {m: importlib.util.find_spec(m) is not None for m in ('numpy','scipy','pyvista','vtk','pandas','getfem')}
for path in sorted((root / 'C3D8v').rglob('*')):
    if path.is_file():
        report['files'][str(path.relative_to(root)).replace('\\', '/')] = {
            'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
for path in source.glob('*.py'):
    compile(path.read_bytes(), str(path), 'exec')
tree = ast.parse((source / 'C3D8-slab.py').read_text())
definitions = ast.Module(body=[n for n in tree.body if isinstance(n, (ast.ClassDef, ast.FunctionDef))], type_ignores=[])
namespace = {**dict(np=np, ABC=ABC, abstractmethod=abstractmethod), **{n: getattr(local,n) for n in dir(local) if not n.startswith('_')}}
exec(compile(definitions, str(source / 'C3D8-slab.py'), 'exec'), namespace)
Mesh = namespace['mesh_c3d8_enum_xzy']

def spectral(matrix):
    values = np.linalg.eigvalsh((matrix + matrix.T) / 2)
    threshold = max(abs(values)) * 1e-10
    return dict(rank=int(np.count_nonzero(abs(values) > threshold)), size=len(values),
                negative=int(np.count_nonzero(values < -threshold)),
                symmetryRelative=float(np.max(abs(matrix-matrix.T))/max(1., np.max(abs(matrix)))))

for a,b,c in [(175.,175.,50.), (170.,280.,63.), (1.,2.,3.)]:
    D = local.Dmat(24000., .3)
    integrated = np.zeros((24,24))
    for xi in (-1/np.sqrt(3),1/np.sqrt(3)):
        for eta in (-1/np.sqrt(3),1/np.sqrt(3)):
            for zeta in (-1/np.sqrt(3),1/np.sqrt(3)):
                B = local.Blocal(a*(xi+1)/2,b*(eta+1)/2,c*(zeta+1)/2,a,b,c)
                integrated += B.T @ D @ B * a*b*c/8
    K = local.Klocal(24000., .3, a,b,c)
    Kr = local.KReducedlocal(24000., .3, a,b,c)
    B = local.Blocal(a/2,b/2,c/2,a,b,c)
    force = local.Flocal(a,b,.1*a,.8*a,.2*b,.9*b,.8)
    Ks = local.Kslocal(a,b,c,10,20,30)
    Ksr = local.KsReducedlocal(a,b,c,10,20,30)
    reduced_expected = B.T@D@B*a*b*c
    surface_center = np.zeros((3,24))
    for n in range(4,8):
        surface_center[:,3*n:3*n+3] = np.eye(3)/4
    surface_expected = surface_center.T @ np.diag([10,20,30]) @ surface_center * a*b
    row = dict(size=[a,b,c], full=spectral(K), reduced=spectral(Kr), foundationFull=spectral(Ks),
               foundationReduced=spectral(Ksr), fullWithFoundation=spectral(K+Ks), reducedWithFoundation=spectral(Kr+Ksr),
               fullQuadratureRelative=float(np.max(abs(K-integrated))/np.max(abs(K))),
               reducedQuadratureRelative=float(np.max(abs(Kr-B.T@D@B*a*b*c))/np.max(abs(Kr))),
               eightTimesReducedMatchesMidpointRelative=float(np.max(abs(8*Kr-reduced_expected))/np.max(abs(reduced_expected))),
               fourTimesReducedFoundationMatchesMidpointRelative=float(np.max(abs(4*Ksr-surface_expected))/np.max(abs(surface_expected))),
               verticalForce=float(force[2::3].sum()), expectedForce=.8*.7*a*.7*b,
               loadedLocalNodes=np.flatnonzero(force.reshape(8,3)[:,2]).tolist(),
               foundationActiveLocalDofs=np.flatnonzero(np.any(Ks != 0,axis=1)).tolist())
    xyz = np.array([[0,0,0],[a,0,0],[a,b,0],[0,b,0],[0,0,c],[a,0,c],[a,b,c],[0,b,c]])
    affine = np.column_stack((2*xyz[:,0]+3*xyz[:,1], 5*xyz[:,1]+7*xyz[:,2], 11*xyz[:,2]+13*xyz[:,0])).ravel()
    row['affineStrain'] = (B@affine).tolist()
    report['elements'].append(row)

for nx,ny,nz in [(2,2,2),(3,2,3),(3,3,3)]:
    mesh = Mesh(np.linspace(0,340,nx), np.linspace(-20,540,ny), np.linspace(0,126,nz))
    coords = mesh.getAllNodeCoordinates()
    # RectilinearGrid(ynodes,xnodes,znodes) point order: first argument varies fastest.
    vtk_coords = np.array([[y,x,z] for z in mesh.znodes for x in mesh.xnodes for y in mesh.ynodes])
    row = dict(axes=[nx,ny,nz], nodes=mesh.getNumNodes(), elements=mesh.getNumElements(),
               firstConnectivity=mesh.getNodesOfElement(0).tolist(), firstCoordinates=coords[:5].tolist(),
               dormantVtkTupleCoordinateMismatches=int(np.count_nonzero(np.any(coords != vtk_coords,axis=1))),
               bottomElements=mesh.getBottomElements().tolist(), topElements=mesh.partitionRectangleToElements(np.array([0,340,-20,540])).tolist())
    for reduced in (False,True):
        K = np.zeros((mesh.getNumDofs(),mesh.getNumDofs()))
        for e in range(mesh.getNumElements()):
            dofs=mesh.getDofsOfElement(e); size=mesh.getSizeOfElement(e)
            K[np.ix_(dofs,dofs)] += (local.KReducedlocal if reduced else local.Klocal)(24000,.3,*size)
        for e in mesh.getBottomElements():
            dofs=mesh.getDofsOfElement(e); size=mesh.getSizeOfElement(e)
            K[np.ix_(dofs,dofs)] += (local.KsReducedlocal if reduced else local.Kslocal)(*size,50,50,50)
        row['reduced' if reduced else 'full'] = spectral(K)
    report['meshes'].append(row)
data=json.loads((source/'inputs.json').read_text())
counts=[len(data['nodes'][axis]) for axis in 'xyz']
report['suppliedInput'] = dict(axes=counts,nodes=int(np.prod(counts)),elements=int(np.prod(np.array(counts)-1)),
                             dofs=3*int(np.prod(counts)),totalForce=sum(p['q']*(p['x2']-p['x1'])*(p['y2']-p['y1']) for p in data['loads']))
working = root/'build/c3d8-intake'
working.mkdir(parents=True,exist_ok=True)
# Attempt the original entry point only when missing PyVista guarantees failure
# before the large supplied model is assembled. Full execution belongs to Phase C1.
if not report['dependenciesAvailable']['pyvista']:
    attempt = subprocess.run([sys.executable,'-B',str(source/'C3D8-slab.py'),str(source/'inputs.json')],
                             cwd=working,capture_output=True,text=True,timeout=15)
    report['originalCliAttempt'] = dict(exitCode=attempt.returncode,stdout=attempt.stdout,stderr=attempt.stderr)
destination = root/'docs/C3D8_INTAKE_AUDIT.json'
destination.write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k != 'files'},indent=2))
