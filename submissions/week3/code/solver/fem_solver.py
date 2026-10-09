"""Shared frozen entry point for the Plate and C3D8 production backends."""
import argparse
from pathlib import Path
import sys

_ROOT = Path(__file__).resolve().parent
for _folder in (_ROOT / 'reference', _ROOT / 'c3d8'):
    sys.path.insert(0, str(_folder))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, add_help=False)
    parser.add_argument('--backend', required=True, choices=('plate', 'c3d8'))
    selected, remaining = parser.parse_known_args(argv)
    if selected.backend == 'plate':
        from plate_solver import main as backend_main
    else:
        from c3d8_solver import main as backend_main
    return backend_main(remaining)


if __name__ == '__main__':
    sys.exit(main())
