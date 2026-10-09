from pathlib import Path

root = Path(SPECPATH).parent
solver = root / 'solver'
plate = solver / 'reference'
c3d8 = solver / 'c3d8'
sources = [*plate.glob('*.py'), *c3d8.glob('*.py')]
a = Analysis([str(solver / 'fem_solver.py')], pathex=[str(plate), str(c3d8)],
             binaries=[], datas=[(str(path), '.') for path in sources],
             hiddenimports=['plate_solver', 'plate_analysis', 'fem_local_Plate20_DOF5_07_09_2025',
                            'c3d8_solver', 'c3d8_analysis', 'C3D8local'],
             hooksconfig={'matplotlib': {'backends': ['Agg']}},
             excludes=['PyQt5', 'PyQt6', 'PySide2', 'PySide6', 'tkinter', 'pytest', 'IPython',
                       'torch', 'tensorflow', 'jax', 'jaxlib', 'sympy', 'pandas', 'numba'], noarchive=False)
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, [], exclude_binaries=True, name='FemSolver',
          debug=False, strip=False, upx=False, console=True)
coll = COLLECT(exe, a.binaries, a.datas, strip=False, upx=False, name='FemSolver')
