from pathlib import Path

root = Path(SPECPATH).parent
reference = root / 'solver' / 'reference'
a = Analysis([str(reference / 'plate_solver.py')], pathex=[str(reference)],
             binaries=[], datas=[(str(p), '.') for p in reference.glob('*.py')],
             hiddenimports=[], hooksconfig={'matplotlib': {'backends': ['Agg']}},
             excludes=['PyQt5', 'PyQt6', 'PySide2', 'PySide6', 'tkinter', 'pytest', 'IPython',
                       'torch', 'tensorflow', 'jax', 'jaxlib', 'sympy', 'pandas', 'numba'], noarchive=False)
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, [], exclude_binaries=True, name='PlateSolver',
          debug=False, strip=False, upx=False, console=True)
coll = COLLECT(exe, a.binaries, a.datas, strip=False, upx=False, name='PlateSolver')
