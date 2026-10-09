"""Callable full-integration C3D8 analysis derived from the received driver."""
from pathlib import Path
import numpy as np
import time
from scipy.sparse import lil_matrix
from scipy.sparse.linalg import MatrixRankWarning, spsolve
import warnings

from C3D8local import Klocal, Kslocal, Flocal, Blocal, Dmat


class AnalysisCancelled(Exception):
    pass


class Mesh:
    def __init__(self, x, y, z):
        self.x, self.y, self.z = map(lambda v: np.asarray(v, dtype=float), (x, y, z))
        self.nx, self.ny, self.nz = len(self.x), len(self.y), len(self.z)
        self.nex, self.ney, self.nez = self.nx-1, self.ny-1, self.nz-1
    @property
    def node_count(self): return self.nx*self.ny*self.nz
    @property
    def element_count(self): return self.nex*self.ney*self.nez
    def node(self, i, j, k): return j*self.nx*self.nz+k*self.nx+i
    def element_indices(self, number):
        side=self.nex*self.nez; j=number//side; rem=number%side
        return rem%self.nex, j, rem//self.nex
    def nodes(self, number):
        i,j,k=self.element_indices(number)
        return np.array([self.node(i,j,k),self.node(i+1,j,k),self.node(i+1,j+1,k),self.node(i,j+1,k),
                         self.node(i,j,k+1),self.node(i+1,j,k+1),self.node(i+1,j+1,k+1),self.node(i,j+1,k+1)])
    def dofs(self, number):
        return (3*self.nodes(number)[:,None]+np.arange(3)).ravel()
    def size(self, number):
        i,j,k=self.element_indices(number)
        return self.x[i+1]-self.x[i], self.y[j+1]-self.y[j], self.z[k+1]-self.z[k]
    def coordinates(self):
        return np.array([[x,y,z] for y in self.y for z in self.z for x in self.x])


def _event(emit, phase, completed=None, total=None):
    event={'type':'phase','phase':phase}
    if completed is not None: event.update(completed=completed,total=total)
    else: event['progressKnown']=False
    emit(event)


def _cancel(check):
    if check(): raise AnalysisCancelled()


def run_analysis(data, output_dir, emit=lambda event: None, cancellation_requested=lambda: False):
    started=time.perf_counter(); timings={}
    mesh=Mesh(data['nodes']['x'],data['nodes']['y'],data['nodes']['z'])
    E,nu=data['material']['Emod'],data['material']['nu']; sub=data['subgrade']
    ndof=3*mesh.node_count
    K=lil_matrix((ndof,ndof),dtype=float)
    solid_stiffness_cache={}; foundation_stiffness_cache={}; load_vector_cache={}
    _event(emit,'solid-assembly',0,mesh.element_count)
    for e in range(mesh.element_count):
        _cancel(cancellation_requested)
        dofs=mesh.dofs(e); a,b,c=mesh.size(e); key=(a,b,c)
        ke=solid_stiffness_cache.get(key)
        if ke is None:
            ke=Klocal(E,nu,a,b,c); solid_stiffness_cache[key]=ke
        for local_i,I in enumerate(dofs): K[I,dofs]+=ke[local_i]
        if e%max(1,mesh.element_count//100)==0 or e+1==mesh.element_count:
            _event(emit,'solid-assembly',e+1,mesh.element_count)
    timings['solidAssembly']=time.perf_counter()-started
    foundation=[e for e in range(mesh.element_count) if mesh.element_indices(e)[2]==mesh.nez-1]
    _event(emit,'foundation-assembly',0,len(foundation))
    for n,e in enumerate(foundation):
        _cancel(cancellation_requested)
        dofs=mesh.dofs(e); a,b,c=mesh.size(e); key=(a,b,c)
        ke=foundation_stiffness_cache.get(key)
        if ke is None:
            ke=Kslocal(a,b,c,sub['Kx'],sub['Ky'],sub['Kz']); foundation_stiffness_cache[key]=ke
        for local_i,I in enumerate(dofs): K[I,dofs]+=ke[local_i]
        if n%max(1,len(foundation)//100)==0 or n+1==len(foundation):
            _event(emit,'foundation-assembly',n+1,len(foundation))
    timings['foundationAssembly']=time.perf_counter()-started-sum(timings.values())
    F=np.zeros(ndof)
    _event(emit,'load-assembly',0,max(1,len(data['loads'])))
    for load_index,load in enumerate(data['loads']):
        _cancel(cancellation_requested)
        for j in range(mesh.ney):
            for i in range(mesh.nex):
                x1=max(mesh.x[i],load['x1']); x2=min(mesh.x[i+1],load['x2'])
                y1=max(mesh.y[j],load['y1']); y2=min(mesh.y[j+1],load['y2'])
                if x2<=x1 or y2<=y1: continue
                e=j*mesh.nex*mesh.nez+i
                a,b,_=mesh.size(e)
                local=(a,b,x1-mesh.x[i],x2-mesh.x[i],y1-mesh.y[j],y2-mesh.y[j],load['q'])
                force=load_vector_cache.get(local)
                if force is None:
                    force=Flocal(*local); load_vector_cache[local]=force
                F[mesh.dofs(e)]+=force
        _event(emit,'load-assembly',load_index+1,len(data['loads']))
    timings['loadAssembly']=time.perf_counter()-started-sum(timings.values())
    _cancel(cancellation_requested); _event(emit,'solve')
    K=K.tocsr()
    with warnings.catch_warnings():
        warnings.simplefilter('error',MatrixRankWarning)
        U=np.asarray(spsolve(K,F))
    if not np.isfinite(U).all(): raise FloatingPointError('The C3D8 solution contains non-finite values')
    timings['solve']=time.perf_counter()-started-sum(timings.values())
    residual=K@U-F
    matrix_scale=float(np.max(np.asarray(abs(K).sum(axis=1)).ravel(),initial=0))
    residual_scale=matrix_scale*float(np.max(np.abs(U),initial=0))+float(np.max(np.abs(F),initial=0))
    scaled_residual=float(np.max(np.abs(residual),initial=0)/max(residual_scale,np.finfo(float).tiny))
    asymmetry=K-K.T
    symmetry_relative=float(np.max(np.abs(asymmetry.data),initial=0)/max(np.max(np.abs(K.data),initial=0),np.finfo(float).tiny))
    spring_reaction=np.zeros(ndof)
    for e in foundation:
        dofs=mesh.dofs(e); a,b,c=mesh.size(e)
        spring_reaction[dofs]-=Kslocal(a,b,c,sub['Kx'],sub['Ky'],sub['Kz'])@U[dofs]
    nodal_force=F.reshape(-1,3); nodal_reaction=spring_reaction.reshape(-1,3); coords=mesh.coordinates()
    applied=nodal_force.sum(axis=0); reaction=nodal_reaction.sum(axis=0)
    applied_moment=np.cross(coords,nodal_force).sum(axis=0); reaction_moment=np.cross(coords,nodal_reaction).sum(axis=0)
    force_balance=float(np.linalg.norm(applied+reaction)/max(np.linalg.norm(applied),1.0))
    length=max(1.0,float(np.max(np.ptp(coords,axis=0))))
    moment_balance=float(np.linalg.norm(applied_moment+reaction_moment)/max(np.linalg.norm(applied_moment),np.linalg.norm(applied)*length,1.0))
    _cancel(cancellation_requested); _event(emit,'recovery',0,mesh.element_count)
    strain=np.zeros((mesh.node_count,6)); stress=np.zeros_like(strain); counts=np.zeros(mesh.node_count)
    D=Dmat(E,nu); coords=mesh.coordinates()
    for e in range(mesh.element_count):
        _cancel(cancellation_requested); nodes=mesh.nodes(e); a,b,c=mesh.size(e); ue=U[mesh.dofs(e)]
        origin=coords[nodes[0]]
        for node in nodes:
            local=coords[node]-origin; value=Blocal(local[0],local[1],local[2],a,b,c)@ue
            strain[node]+=value; stress[node]+=D@value; counts[node]+=1
        if e%max(1,mesh.element_count//100)==0 or e+1==mesh.element_count: _event(emit,'recovery',e+1,mesh.element_count)
    strain/=counts[:,None]; stress/=counts[:,None]
    timings['recovery']=time.perf_counter()-started-sum(timings.values())
    fields={'u':U.reshape(-1,3)[:,0],'v':U.reshape(-1,3)[:,1],'w':U.reshape(-1,3)[:,2]}
    for names,values in ((('sxx','syy','szz','sxy','syz','szx'),stress),(('exx','eyy','ezz','exy','eyz','ezx'),strain)):
        fields.update({name:values[:,i] for i,name in enumerate(names)})
    output_dir=Path(output_dir); output_dir.mkdir(parents=True,exist_ok=True)
    write_vtk(output_dir/'results.vtk',mesh,coords,fields)
    timings['export']=time.perf_counter()-started-sum(timings.values())
    return {'nodes':mesh.node_count,'elements':mesh.element_count,'dofs':ndof,
            'maxAbsDisplacement':float(np.max(np.abs(U),initial=0)), 'fields':list(fields),
            'stiffnessNonzeros':int(K.nnz),
            'storedSystemBytes':int(K.data.nbytes+K.indices.nbytes+K.indptr.nbytes+U.nbytes+F.nbytes),
            'cachedKernels':{'solid':len(solid_stiffness_cache),'foundation':len(foundation_stiffness_cache),
                             'loads':len(load_vector_cache)},
            'checks':{'scaledResidual':scaled_residual,'symmetryRelative':symmetry_relative,
                      'appliedForceXYZ':applied.tolist(),'springReactionXYZ':reaction.tolist(),
                      'forceBalanceRelative':force_balance,'momentBalanceRelative':moment_balance},
            'timingsSeconds':{**timings,'total':time.perf_counter()-started}}


def write_vtk(path,mesh,coords,fields):
    with Path(path).open('w',encoding='ascii',newline='\n') as f:
        f.write('# vtk DataFile Version 3.0\nC3D8 results\nASCII\nDATASET UNSTRUCTURED_GRID\n')
        f.write(f'POINTS {mesh.node_count} double\n')
        for p in coords: f.write(f'{p[0]:.17g} {p[1]:.17g} {p[2]:.17g}\n')
        f.write(f'CELLS {mesh.element_count} {mesh.element_count*9}\n')
        for e in range(mesh.element_count): f.write('8 '+' '.join(map(str,mesh.nodes(e)))+'\n')
        f.write(f'CELL_TYPES {mesh.element_count}\n'+'12\n'*mesh.element_count)
        f.write(f'CELL_DATA {mesh.element_count}\nSCALARS solver_element_id long 1\nLOOKUP_TABLE default\n')
        for e in range(mesh.element_count): f.write(f'{e}\n')
        f.write(f'POINT_DATA {mesh.node_count}\n')
        f.write('SCALARS solver_node_id long 1\nLOOKUP_TABLE default\n')
        for n in range(mesh.node_count): f.write(f'{n}\n')
        for name,values in fields.items():
            f.write(f'SCALARS {name} double 1\nLOOKUP_TABLE default\n')
            for value in values: f.write(f'{value:.17g}\n')
        f.write('VECTORS displacement double\n')
        for u,v,w in zip(fields['u'],fields['v'],fields['w']): f.write(f'{u:.17g} {v:.17g} {w:.17g}\n')
