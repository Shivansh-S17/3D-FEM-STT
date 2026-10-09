"""Software qualification cases; not author-approved engineering benchmarks."""
import copy


def cases():
    base = {
        'nodes': {'x': [0, 500, 1000], 'y': [0, 500, 1000]},
        'slab': {'Emod': 30000, 'nu': 0.2, 't': 200},
        'subgrade': {'Kx': 1, 'Ky': 1, 'Kz': 1},
        'loads': {'x1': [250], 'x2': [750], 'y1': [250], 'y2': [750], 'q': [0.7]},
    }
    asymmetric = copy.deepcopy(base)
    asymmetric['nodes'] = {'x': [0, 170, 550, 1100], 'y': [-200, 80, 600]}
    asymmetric['slab']['t'] = 175
    asymmetric['subgrade'] = {'Kx': 0.6, 'Ky': 1.2, 'Kz': 0.02}
    asymmetric['loads'] = {'x1': [120], 'x2': [790], 'y1': [-50], 'y2': [430], 'q': [0.65]}
    overlapping = copy.deepcopy(asymmetric)
    for key, value in dict(x1=400, x2=950, y1=100, y2=510, q=0.35).items():
        overlapping['loads'][key].append(value)
    zero = copy.deepcopy(base)
    zero['loads']['q'] = [0]
    uniform = copy.deepcopy(base)
    uniform['loads'] = dict(x1=[0], x2=[1000], y1=[0], y2=[1000], q=[0.7])
    edge = copy.deepcopy(asymmetric)
    edge['loads'] = dict(x1=[0], x2=[170], y1=[-200], y2=[80], q=[0.3])
    return dict(symmetric=base, nonuniform=asymmetric, overlapping=overlapping,
                zero=zero, uniform=uniform, edge=edge)


def tolerance(name):
    # Provisional numerical-regression tolerances, absolute in each field's unit.
    if name.startswith('theta_'):
        return 1e-11, 1e-6
    if name.startswith(('sxx', 'syy', 'sxy')):
        return 1e-7, 1e-6
    return 1e-8, 1e-6
