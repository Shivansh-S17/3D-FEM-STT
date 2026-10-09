"""Load numerical/mesh reference definitions without the optional plotting stack.

This helper is for unit probes only. Real baseline runs execute the original
entry point with all of its real imports through qualify-c3d8-reference.py.
"""
import ast
from abc import ABC, abstractmethod
from pathlib import Path
import sys
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'C3D8v/python'))
import C3D8local as element


def definitions():
    path = ROOT / 'C3D8v/python/C3D8-slab.py'
    tree = ast.parse(path.read_text(encoding='utf-8'))
    subset = ast.Module(body=[n for n in tree.body if isinstance(n, (ast.ClassDef, ast.FunctionDef))], type_ignores=[])
    namespace = {'np': np, 'ABC': ABC, 'abstractmethod': abstractmethod,
                 **{n: getattr(element, n) for n in dir(element) if not n.startswith('_')}}
    exec(compile(subset, str(path), 'exec'), namespace)
    return namespace
