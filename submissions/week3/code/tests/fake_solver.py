"""Process-lifecycle fixture. Deliberately does not implement FEM mathematics."""
import json
import os
from pathlib import Path
import sys
import time

spec = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
scenario = spec.get("scenario", "missing")
if scenario == "hang":
    time.sleep(60)
elif scenario == "exit7":
    print("controlled failure", file=sys.stderr, flush=True)
    sys.exit(7)
elif scenario == "unicode":
    for byte in "split → café\nlast line".encode("utf-8"):
        os.write(1, bytes([byte]))
        time.sleep(0.003)
elif scenario == "success":
    import shutil
    Path("output_python_square").mkdir()
    shutil.copyfile(spec["fixture"], "output_python_square/results.vtk")
