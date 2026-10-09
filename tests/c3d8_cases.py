"""C3D8 reference inputs, separate from approved engineering benchmarks."""
import copy
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def cases():
    single = {
        'nodes': {'x': [0., 170.], 'y': [0., 280.], 'z': [0., 63.]},
        'material': {'Emod': 24000., 'nu': .3},
        'subgrade': {'Kx': 50., 'Ky': 50., 'Kz': 50.},
        'loads': [{'q': .8, 'x1': 0., 'x2': 170., 'y1': 0., 'y2': 280.}],
        'options': {'reduced_integration': False},
    }
    asymmetric = copy.deepcopy(single)
    asymmetric['nodes'] = {'x': [10., 180., 460.], 'y': [-20., 260.], 'z': [5., 68., 151.]}
    asymmetric['subgrade'] = {'Kx': 20., 'Ky': 35., 'Kz': 50.}
    asymmetric['loads'] = [dict(q=.8, x1=30., x2=330., y1=10., y2=220.),
                           dict(q=.35, x1=130., x2=440., y1=100., y2=240.)]
    compression = copy.deepcopy(asymmetric)
    compression['material']['nu'] = 0.
    compression['loads'] = [dict(q=.8, x1=10., x2=460., y1=-20., y2=260.)]
    zero = copy.deepcopy(asymmetric)
    zero['loads'] = []
    return {'single_brick': single, 'asymmetric_overlap': asymmetric,
            'analytical_compression': compression, 'zero_load': zero,
            'supplied_21x21x5': json.loads((ROOT / 'C3D8v/python/inputs.json').read_text())}
