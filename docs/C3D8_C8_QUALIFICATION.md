# C3D8 C8 — numerical and performance qualification

This technical phase record describes its own implementation and verification scope. See [current implementation status](IMPLEMENTATION_STATUS.md) for the Week 3 capabilities and build/package qualification.


**Status:** software qualification complete; independent physical acceptance open

The production solver now records checks that are recomputed from its assembled
system and solution: stiffness symmetry, a scaled algebraic residual, applied
force, independently reassembled spring reaction, force balance and moment
balance. It also records nonzero count, stored CSR/vector bytes and timings for
solid assembly, foundation assembly, load assembly, solve, recovery and export.

The new `C3D8Qualification` suite verifies:

- pressure linearity and two-patch superposition for all 15 scalar fields;
- the closed-form `nu=0` compression solution under simultaneous X/Y/Z
  refinement through 1, 8 and 64 elements;
- positive definiteness and finite condition estimates for tested Poisson ratios
  0, 0.3, 0.45 and 0.49 and element depths 10, 63 and 280 mm;
- production equivalence with the unchanged received result at the supplied
  21×21×5-node case;
- successful production execution and numerical acceptance of the imported
  21×21×6-node user case;
- scaled residual, force balance and moment balance below `1e-11`;
- acceptance of larger structured inputs without an application-imposed size
  ceiling.

The supplied case contains 2,205 nodes, 1,600 elements and 6,615 DOFs. A local
production-wrapper observation on 13 September 2026 completed in 17.83 seconds,
with 293,463 stiffness nonzeros and 3,653,860 bytes for the stored CSR matrix and
solution/load vectors. The earlier isolated received-code measurement was 12.24
seconds and a 184.8 MiB peak process-tree working set. These are single-machine,
warm-cache observations; the stored-array figure excludes Python, LIL assembly
and sparse-factorization overhead.

The imported 21×21×6-node case contains 2,646 nodes, 2,000 elements and 7,938
DOFs. The production analysis completed in 15.33 seconds after kernel caching,
with 358,032 stiffness nonzeros, 4,455,148 bytes in its stored CSR/vector arrays,
a `3.71e-16` scaled residual, and force/moment imbalance below `1.6e-15`.
An isolated repeat completed in 17.66 seconds with a 103.8 MiB peak process
working set measured at 20 ms intervals. These are single-machine observations.

The GUI and strict CLI do not impose a model-size or DOF ceiling. Completed runs
still enforce all numerical acceptance checks. The recorded cases are performance
measurements rather than limits.

The following release-acceptance items remain open:

- author confirmation of physical units, source-Z direction and foundation
  convention;
- a working, load-verified GetFEM case or another author-approved independent
  nonuniform benchmark;
- approved per-quantity engineering tolerances and a physical convergence study
  after coordinate, shear and recovery conventions are aligned.

The analytical compression case is independent but deliberately limited. The
software can proceed to C9 packaging with C3D8 described as provisional; it
must not be advertised as independently engineering-validated until the open
items above are resolved.
