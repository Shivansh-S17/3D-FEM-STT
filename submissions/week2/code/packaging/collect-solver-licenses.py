"""Copy installed dependency notices and record the exact packaging environment."""
import ast
import importlib.metadata as metadata
import json
from pathlib import Path
import shutil
import sys

destination = Path(sys.argv[1])
destination.mkdir(parents=True, exist_ok=True)
packages = set(('numpy', 'scipy', 'matplotlib', 'contourpy', 'cycler', 'fonttools',
            'kiwisolver', 'packaging', 'pillow', 'pyparsing', 'python-dateutil',
            'six', 'pywin32', 'pyinstaller', 'pyinstaller-hooks-contrib'))
analysis_path = Path(__file__).resolve().parents[1] / 'build/solver-freeze/plate-solver/Analysis-00.toc'
analysis = ast.literal_eval(analysis_path.read_text(encoding='utf-8'))
modules = set()
for table in analysis:
    if not isinstance(table, list):
        continue
    for item in table:
        if isinstance(item, tuple) and len(item) == 3 and item[2] in ('PYMODULE', 'EXTENSION'):
            modules.add(item[0].replace('\\', '.').replace('/', '.').split('.')[0])
distributions = metadata.packages_distributions()
for module in modules:
    packages.update(distributions.get(module, ()))
manifest = {'python': sys.version, 'packages': {}, 'bundledTopLevelModules': sorted(modules)}
python_license = Path(sys.base_prefix) / 'LICENSE.txt'
shutil.copy2(python_license, destination / 'Python-LICENSE.txt')
for name in sorted(packages):
    distribution = metadata.distribution(name)
    notices = []
    for item in distribution.files or []:
        if not any(word in item.name.lower() for word in ('license', 'copying', 'notice')):
            continue
        source = Path(distribution.locate_file(item))
        if not source.is_file():
            continue
        # Keep nested notices distinct without trusting relative parent traversal.
        target = destination / name / str(item).replace('\\', '__').replace('/', '__')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        notices.append(target.relative_to(destination).as_posix())
    manifest['packages'][name] = {'version': distribution.version, 'notices': notices}
(destination / 'build-environment.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
