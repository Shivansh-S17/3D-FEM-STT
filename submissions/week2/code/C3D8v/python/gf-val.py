import getfem as gf
import numpy as np
import matplotlib.pyplot as plt


def export_to_csv(mesh, u, strain, stress, filename="output-gf.csv"):
    import pandas as pd
    
    coords = mesh.pts().T
    u_sol = u.reshape((mesh.nbpts(),mfu.qdim()))
    eps = strain.reshape((mesh.nbpts(),9))
    sigma = stress.reshape((mesh.nbpts(),9))
    #Order of stress and strain in GetFEM: xx, xy, xz, xy, yy, yz, xz, yz, zz
    
    data = {
        'x' : coords[:,0],
        'y' : coords[:,1],
        'z' : coords[:,2],
        'u': u_sol[:,0],
        'v': u_sol[:,1],
        'w': u_sol[:,2],
        'sxx': sigma[:,0],
        'syy': sigma[:,4],
        'szz': sigma[:,8],
    }
    
    df = pd.DataFrame(data)
    df.to_csv(filename, index=False)
    print(f'Data printed to {filename}')


xnodes = np.array([0.0, 175.0, 350.0, 525.0, 700.0, 875.0, 1050.0, 1225.0, 1400.0, 1575.0, 1750.0, 1925.0, 2100.0, 2275.0, 2450.0, 2625.0, 2800.0, 2975.0, 3150.0, 3325.0, 3500.0])
ynodes = np.array([0.0, 175.0, 350.0, 525.0, 700.0, 875.0, 1050.0, 1225.0, 1400.0, 1575.0, 1750.0, 1925.0, 2100.0, 2275.0, 2450.0, 2625.0, 2800.0, 2975.0, 3150.0, 3325.0, 3500.0])
znodes = np.array([0.0, 50.0, 100.0, 150.0, 200.0])
mesh = gf.Mesh('cartesian', xnodes, ynodes, znodes)
  
elements_degree = 1
mfu = gf.MeshFem(mesh, 3)
mfu.set_classical_fem(elements_degree)
mim = gf.MeshIm(mesh, elements_degree*2) #2 Gauss pts per direction, set to 1 to get reduced integration
print(mfu.display())
print(mim.integ())

#Boundaries
TOP_FACE = 1
BOTTOM_FACE = 2
mesh.set_region(TOP_FACE, mesh.outer_faces_with_direction([0,0,1], 0))
mesh.set_region(BOTTOM_FACE, mesh.outer_faces_with_direction([0,0,-1], 0))
pids = mesh.pid_in_regions(TOP_FACE)
#print(pids)


md = gf.Model('real')
md.add_fem_variable('u', mfu)

E = 24000
nu = 0.3
kx = 50
ky = 50
kz = 50
lambda_ = E*nu/((1+nu)*(1-2*nu))
mu = E/(2*(1+nu))
md.add_initialized_data('lambda', [lambda_])
md.add_initialized_data('mu', [mu])
md.add_initialized_data('kx', [kx])
md.add_initialized_data('ky', [ky])
md.add_initialized_data('kz', [kz])
md.add_isotropic_linearized_elasticity_brick(mim, 'u', 'lambda', 'mu')
md.add_linear_term(mim, 'kx * u(1) * Test_u(1)', BOTTOM_FACE) # Horizontal X
md.add_linear_term(mim, 'ky * u(2) * Test_u(2)', BOTTOM_FACE) # Horizontal Y
md.add_linear_term(mim, 'kz * u(3) * Test_u(3)', BOTTOM_FACE) # Vertical Winkler

q = -0.8 #Negative shows direction
md.add_initialized_data('q', [q])
#load_string = 'q * Heaviside(X(1) - 1000.0) * Heaviside(1500.0 - X(1)) * Heaviside(X(2) - 1000.0) * Heaviside(1500.0 - X(2)) * Test_u(3)' 
load_string = 'q * Test_u(3)'
md.add_linear_term(mim, load_string, TOP_FACE)
md.solve()
u_sol = md.variable('u')

#print(u_sol.reshape((mesh.nbpts(),mfu.qdim())))

#Post processing
evaluation_points = mesh.pts()
strain_expr = '(Grad_u + Grad_u\') / 2'
strain_interpolated = md.interpolation(strain_expr, evaluation_points, mesh) # Returns a matrix of size (9, num_points) representing the flattened 3x3 strain tensor
stress_expr = 'lambda * Trace((Grad_u + Grad_u\') / 2) * Id(3) + 2 * mu * ((Grad_u + Grad_u\') / 2)'
stress_interpolated = md.interpolation(stress_expr, evaluation_points, mesh) # Returns a matrix of size (9, num_points) representing the flattened 3x3 stress tensor
#Order of stress and strain in GetFEM: xx, xy, xz, xy, yy, yz, xz, yz, zz
export_to_csv(mesh, u_sol, strain_interpolated, stress_interpolated, "output-gf.csv")