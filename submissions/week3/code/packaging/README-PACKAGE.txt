FemGui Windows package
======================

Start FemGui.exe. Keep this entire folder together; the application loads its
Qt/VTK runtime and the shared frozen FemSolver backend from here. The GUI
selects Plate or C3D8 mode from the active project.

C3D8 status: provisional engineering release. The application imposes no
model-size or DOF ceiling. Reduced integration is rejected. Software
regression, analytical compression, equilibrium, refinement and received-source
equivalence checks pass. Author confirmation of units/Z/foundation conventions
and an accepted independent nonuniform benchmark remain open.

Run artifacts, including input, results, logs, hashes and provenance, are stored
beside the saved project under its runs folder.
