# Plate solver process contract, version 1

The packaged GUI dispatches plate runs through
`solver/FemSolver/FemSolver.exe --backend plate`; development builds use
`solver/reference/plate_solver.py`. Both call
`run_analysis(input_path, output_dir, report_event, cancellation_requested,
diagnostics)` in `plate_analysis.py`. Element routines,
assembly, five linear increments, and stress recovery retain the reference
mathematics. Importing either entry-point module no longer creates a results
directory. `plate_analysis.py` is the callable numerical module; use the
production entry point for the strict process contract.

## CLI

```text
python plate_solver.py --input <absolute-json> --output-dir <absolute-new-directory>
    --events-jsonl --diagnostics <none|summary|full> [--cancel-file <path>]
    [--linear-solver <dense|sparse>]
```

Input contains exactly `nodes`, `slab`, `subgrade`, and `loads`, with the keys
specified in integration-plan §3.2. Duplicate keys, booleans/string numbers,
nonfinite values, unsorted/duplicate coordinates, invalid material values,
empty or unequal patch arrays, negative pressure, out-of-domain rectangles,
and unsupported keys are rejected. There is no legacy-file fallback. At least
one patch is required; a stable zero-load case can use `q=0`. The existing
conservative 1 GiB allocation limit for one dense matrix is also checked by
the CLI; this is not a peak-memory guarantee. The GUI warns above the largest
measured case (1089 nodes). See [qualification results](NUMERICAL_QUALIFICATION.md).

The frozen executable accepts the same arguments after the required
`--backend plate` selector, omitting `python plate_solver.py`.
Both entry points support `--check-backend` alone to verify numerical imports.
Dense solving remains the default; sparse solving is a CLI experiment that
reuses one factorization. The manifest records solver choice, timings and
scaled numerical checks. These additions retain contract version 1.

All-zero foundation is explicitly unsupported because this backend has no
support constraints. Nonzero foundation does not by itself establish physical
validity or adequate conditioning; numerical qualification remains pending.

| Exit | Meaning |
|---|---|
| 0 | Export succeeded and the final result directory was published |
| 1 | Unexpected internal failure |
| 2 | Invalid input or command-line usage |
| 3 | Missing numerical/backend dependency |
| 4 | Numerical failure, nonfinite result, or unsupported zero-foundation configuration |
| 5 | Output/filesystem failure, including an existing output destination |
| 130 | Cooperative cancellation |

Missing/unreadable input is an input failure. Argument-parser errors use
stderr and exit 2. Once CLI parsing succeeds, `--events-jsonl` reserves stdout
for flushed versioned JSON objects. Verbose diagnostics go to UTF-8 files,
and unexpected exception traces go to stderr.

## Events and publication

```json
{"contractVersion":1,"type":"phase","phase":"assembly","completed":4,"total":4}
{"contractVersion":1,"type":"phase","phase":"solve","progressKnown":false}
{"contractVersion":1,"type":"error","code":"NUMERICAL_FAILURE","message":"Singular matrix"}
{"contractVersion":1,"type":"completed","manifest":"run-result.json"}
```

Phases are validation, assembly, loads, solve, postprocessing, and export.
The dense factorization remains indeterminate. Counters represent completed
elements/patches, not an invented overall percentage.

Validated input bytes are copied to a fresh sibling `.partial-*` directory.
The numerical arrays are checked for expected sizes and finite values before
VTK serialization. VTK is written as `results.vtk.tmp` and renamed after the
writer returns successfully. `run-result.json` is atomically written with
contract version, completion status, input/result SHA-256, units, counts,
field ranges, Python/dependency versions, and hashes of all three backend
source files. The staging directory is then renamed to the requested final
directory. Existing output destinations are refused. Publication is atomic
within the same filesystem; this is not a power-loss durability guarantee.

The GUI requires normal exit zero, a valid completion event, no error or
malformed event, and a matching manifest and hashes. It then independently
imports and validates the VTK geometry and all required fields. A cancelled
run never auto-attaches results even if the backend finished before observing
the stop request. The GUI's `run.json` is the authoritative user-visible run
status, distinct from the backend's successful-publication manifest.

`summary` is the GUI default: VTK, manifest, input, and human-readable summary.
`none` omits the human-readable summary. `full` adds `results.txt`, pressure
partition plots, and a stiffness sparsity plot. Failed/interrupted staging
folders remain available for diagnosis and are never auto-imported.

## Cancellation and verification

Stop writes `cancel.request` atomically. The backend checks it before/after
numerical stages and between assembly/force/postprocessing units. SIGINT and
SIGTERM also request cooperative cancellation where delivered. A native
factorization cannot check until it returns; the GUI kills its owned process
after a three-second grace period if needed. The backend spawns no child
solver processes. Compatibility drivers retain terminate/kill behavior.

`tests/test_solver_contract.py` covers CLI validation, explicit exits, import
side effects, reference output equivalence, publication, failure/restart,
diagnostics modes, Unicode paths, and cancellation boundaries. Qt integration
tests check real GUI-orchestrated solves and reject forged/malformed process
events and manifests. These establish integration behavior, not independent
engineering accuracy or freedom from every bug.
