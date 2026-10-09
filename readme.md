# Project No.: 3

* **PI :** Prof. Sushobhan Sen
* **PoC :** sushobhan.sen@iitgn.ac.in

## Group Members
|Name|Roll Number|
|---|---|
| **Piyush Choudhary** | **23110247** |
| **Soham Shrivastava**|**23110315**|
|**Shivansh Shukla**|**23110303**|
|**Yash Shashikant Yeole**|**23110367**|
|**Swain Laxmidhar**|**23110326**|

### project details: Creating a GUI for an FEM analysis program

### project description :
Develop a desktop GUI for a Python 3D FEM solver: define inputs, inspect the mesh, run analysis and view results. The received C3D8 driver actively exports CSV; validated VTK output is part of the application integration.

---
## Submissions:

|Deliverable|Due Date|
|---|---|
|[Project Requirements](./submissions/PROJECT_REQUIREMENTS.md)|4 Sept 2026|
|[Design Plan](./submissions/Design_Plan.md)|13 Sept 2026|
|[Week 1 Submission](./submissions/week1/WEEK1_REPORT.md)|25 Sept 2026|
|[Week 2 Submission](./submissions/week2/WEEK2_REPORT.md)|1 Oct 2026|
|[Week 3 Submission](./submissions/week3/WEEK3_REPORT.md)|8 Oct 2026|

---

## Current version

The repository root contains the current Week 3 application source. The [Week 3 packet](submissions/week3/README.md) includes the corresponding source version, report, native screenshots, runnable examples and build/test evidence. [Week 1](submissions/week1/README.md) and [Week 2](submissions/week2/README.md) document the earlier milestones; Week 1 maintenance corrections are recorded with their verification date.

Week 3 adds the production C3D8 solve path, validated VTK results, a guided edit-to-results workflow, solid result inspection and software qualification checks. The supported C3D8 path uses full integration; independent physical acceptance and user feedback remain open. See the [Week 3 report](submissions/week3/WEEK3_REPORT.md) for scope and known defects.

The [changelog](CHANGELOG.md) records changes by submitted week.

## Build and test

Run from PowerShell with the configured MSVC toolchain and dependencies named in the [Week 3 source README](submissions/week3/code/README.md):

```powershell
.\build.ps1 -Config Release
ctest --test-dir .\build\release --output-on-failure
```

The same source can be built from `submissions/week3/code`. Generated build, distribution and run files are excluded from version control.
