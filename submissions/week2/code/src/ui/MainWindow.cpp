#include "ui/MainWindow.h"

#include "ui/InspectorPanel.h"
#include "ui/ProblemsPanel.h"
#include "ui/ProjectPanel.h"
#include "ui/ViewportWidget.h"
#include "ui/PressurePatchDialog.h"
#include "io/ResultExporter.h"

#include "controllers/AnalysisController.h"
#include "controllers/ProjectController.h"
#include "core/PlateDefinition.h"
#include "core/ProjectDocument.h"
#include "core/ResultSet.h"
#include "core/RunRecord.h"
#include "io/ResultImporter.h"
#include "visualization/PlateMeshBuilder.h"
#include "visualization/C3D8MeshBuilder.h"

#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkIdList.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <utility>

#include <QAction>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QUrl>
#include <QTimer>

namespace femgui {

namespace {
constexpr int kDefaultWindowWidth = 1400;
constexpr int kDefaultWindowHeight = 900;

double overlapArea(double exmin, double exmax, double eymin, double eymax,
                    double rxmin, double rxmax, double rymin, double rymax)
{
    double dx = std::min(exmax, rxmax) - std::max(exmin, rxmin);
    double dy = std::min(eymax, rymax) - std::max(eymin, rymin);
    return (dx > 0 && dy > 0) ? dx * dy : 0.0;
}

InspectorPanel::NodeInfo buildNodeInfo(vtkUnstructuredGrid *mesh, vtkIdType nodeId, int nx, int nz, bool solid,
                                        const ResultSet *resultSet)
{
    InspectorPanel::NodeInfo info;
    info.solverId = nodeId;
    if (!mesh || nodeId < 0 || nodeId >= mesh->GetNumberOfPoints())
        return info;

    info.solid = solid;
    info.dofsPerNode = solid ? 3 : 5;
    info.gridI = nx > 0 ? static_cast<int>(nodeId % nx) : -1;
    info.gridK = solid && nx > 0 && nz > 0 ? static_cast<int>((nodeId / nx) % nz) : -1;
    info.gridJ = nx > 0 ? static_cast<int>(nodeId / (solid ? nx * nz : nx)) : -1;
    double point[3];
    mesh->GetPoint(nodeId, point);
    info.x = point[0];
    info.y = point[1];
    info.z = point[2];

    if (resultSet && resultSet->isValid() && nodeId < resultSet->grid()->GetNumberOfPoints()) {
        info.hasResults = true;
        for (const FieldDescriptor &field : resultSet->availableFields()) {
            if (field.componentCount != 1)
                continue;
            vtkDataArray *array = resultSet->grid()->GetPointData()->GetArray(field.arrayName.toUtf8().constData());
            if (array)
                info.fieldValues[field.label] = array->GetTuple1(nodeId);
            info.fieldUnits[field.label] = field.unit;
        }
    }
    return info;
}

InspectorPanel::ElementInfo buildElementInfo(vtkUnstructuredGrid *mesh, vtkIdType elementId,
                                              const ModelDefinition &model)
{
    InspectorPanel::ElementInfo info;
    info.solverId = elementId;
    if (!mesh || elementId < 0 || elementId >= mesh->GetNumberOfCells())
        return info;

    vtkCell *cell = mesh->GetCell(elementId);
    vtkIdList *pointIds = cell->GetPointIds();
    for (vtkIdType i = 0; i < pointIds->GetNumberOfIds(); ++i)
        info.nodeIds.append(pointIds->GetId(i));

    double bounds[6];
    cell->GetBounds(bounds);
    info.xmin = bounds[0]; info.xmax = bounds[1];
    info.ymin = bounds[2]; info.ymax = bounds[3];
    info.zmin = bounds[4]; info.zmax = bounds[5];
    info.width = info.xmax - info.xmin;
    info.height = info.ymax - info.ymin;
    info.depth = info.zmax - info.zmin;
    info.area = info.width * info.height;
    info.volume = info.area * info.depth;
    info.solid = std::holds_alternative<SolidDefinition>(model);
    info.dofsPerNode = info.solid ? 3 : 5;
    if (info.solid) {
        const double smallest = std::min({info.width, info.height, info.depth});
        const double largest = std::max({info.width, info.height, info.depth});
        info.aspectRatio = smallest > 0 ? largest / smallest : 0.0;
        if (auto *array = mesh->GetCellData()->GetArray("loaded_face_element")) info.onLoadedFace = array->GetTuple1(elementId) != 0;
        if (auto *array = mesh->GetCellData()->GetArray("foundation_face_element")) info.onFoundationFace = array->GetTuple1(elementId) != 0;
    } else {
        info.aspectRatio = (info.width > 0 && info.height > 0)
                                ? std::max(info.width / info.height, info.height / info.width) : 0.0;
    }

    const auto &patches = info.solid ? std::get<SolidDefinition>(model).patches : std::get<PlateDefinition>(model).patches;
    if (info.solid && !info.onLoadedFace) return info;
    for (const PressurePatch &patch : patches) {
        if (!patch.enabledForAnalysis)
            continue;
        double overlap = overlapArea(info.xmin, info.xmax, info.ymin, info.ymax,
                                      patch.x1, patch.x2, patch.y1, patch.y2);
        if (overlap > 0.0) {
            info.overlappingPatchNames.append(patch.name);
            info.appliedPressureResultant += overlap * patch.q;
        }
    }
    return info;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("FEM Analysis GUI — Plate Solver Integration"));
    resize(kDefaultWindowWidth, kDefaultWindowHeight);

    m_controller = new ProjectController(this);
    connect(m_controller, &ProjectController::projectReplaced, this, &MainWindow::onProjectReplaced);

    buildCentralWidget();
    buildMenusAndToolbar();
    buildStatusBar();

    m_projectPanel->bindToController(m_controller);
    connectDocumentSignals();
    connectAnalysisSignals();

    restoreLayout();

    rebuildMeshAndScene();
    doValidate();
    updateStatusBar();

    statusBar()->showMessage(tr("Ready"), 3000);
    m_problemsPanel->appendOutput(tr("FEM Analysis GUI started. Backend: plate-python-v1."));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildCentralWidget()
{
    m_projectPanel = new ProjectPanel(this);
    connect(m_projectPanel, &ProjectPanel::statusMessage, this, [this](const QString &message) {
        m_stateLabel->setText(message);
    });
    connect(m_projectPanel, &ProjectPanel::openOutputFolderRequested, this, &MainWindow::doOpenOutputFolder);
    connect(m_projectPanel, &ProjectPanel::loadRunRequested, this, [this](const QString &runId) {
        auto result = m_controller->loadRunResults(runId);
        if (!result.success) m_problemsPanel->addProblem(ProblemsPanel::Severity::Error, result.error, "run");
        else m_centerTabs->setCurrentWidget(m_resultsViewport);
    });

    m_modelViewport = new ViewportWidget(ViewportWidget::ViewMode::Model, this);
    m_meshViewport = new ViewportWidget(ViewportWidget::ViewMode::Mesh, this);
    m_resultsViewport = new ViewportWidget(ViewportWidget::ViewMode::Results, this);
    connect(m_modelViewport, &ViewportWidget::rectangleDrafted, this, &MainWindow::acceptDrawnPatch);
    connect(m_modelViewport, &ViewportWidget::patchPicked, this, &MainWindow::onPatchPicked);
    connect(m_modelViewport, &ViewportWidget::selectionCleared, this, &MainWindow::onSelectionCleared);
    connect(m_projectPanel, &ProjectPanel::drawPatchRequested, this, &MainWindow::doDrawPatch);
    connect(m_projectPanel, &ProjectPanel::patchSelected, this, &MainWindow::onPatchPicked);
    for (auto *viewport : {m_modelViewport, m_meshViewport, m_resultsViewport})
        connect(viewport, &ViewportWidget::statusMessage, this, [this](const QString &message) { m_stateLabel->setText(message); });

    for (ViewportWidget *viewport : {m_meshViewport, m_resultsViewport}) {
        connect(viewport, &ViewportWidget::nodePicked, this, &MainWindow::onNodePicked);
        connect(viewport, &ViewportWidget::elementPicked, this, &MainWindow::onElementPicked);
        connect(viewport, &ViewportWidget::selectionCleared, this, &MainWindow::onSelectionCleared);
    }

    m_centerTabs = new QTabWidget(this);
    m_centerTabs->addTab(m_modelViewport, tr("Model"));
    m_centerTabs->addTab(m_meshViewport, tr("Mesh"));
    m_centerTabs->addTab(m_resultsViewport, tr("Results"));
    connect(m_centerTabs, &QTabWidget::currentChanged, this, [this] { m_modelViewport->cancelRectangleDrawing(); onSelectionCleared(); });

    m_inspectorPanel = new InspectorPanel(this);
    m_problemsPanel = new ProblemsPanel(this);
    connect(m_problemsPanel, &ProblemsPanel::navigateToField, this, [this](const QString &fieldKey) {
        m_projectPanel->navigateToField(fieldKey);
    });
    connect(m_problemsPanel, &ProblemsPanel::problemCountsChanged, this, [this](int errors, int warnings) {
        m_problemsLabel->setText(tr("%1 errors, %2 warnings").arg(errors).arg(warnings));
    });

    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->addWidget(m_projectPanel);
    m_mainSplitter->addWidget(m_centerTabs);
    m_mainSplitter->addWidget(m_inspectorPanel);
    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 3);
    m_mainSplitter->setStretchFactor(2, 1);

    m_verticalSplitter = new QSplitter(Qt::Vertical, this);
    m_verticalSplitter->addWidget(m_mainSplitter);
    m_verticalSplitter->addWidget(m_problemsPanel);
    m_verticalSplitter->setStretchFactor(0, 4);
    m_verticalSplitter->setStretchFactor(1, 1);

    setCentralWidget(m_verticalSplitter);
}

void MainWindow::buildMenusAndToolbar()
{
    // ---- File ----
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    m_newAction = fileMenu->addAction(tr("&New Project"), QKeySequence::New, this, &MainWindow::doNewProject);
    m_openAction = fileMenu->addAction(tr("&Open Project..."), QKeySequence::Open, this, &MainWindow::doOpenProject);
    fileMenu->addSeparator();
    m_saveAction = fileMenu->addAction(tr("&Save"), QKeySequence::Save, this, [this] { doSaveProject(); });
    fileMenu->addAction(tr("Save &As..."), QKeySequence::SaveAs, this, [this] { doSaveProjectAs(); });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Export Solver &JSON..."), this, &MainWindow::doExportSolverJson);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    // ---- Edit ----
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    m_undoAction = editMenu->addAction(tr("&Undo"), QKeySequence::Undo, this, [this] {
        if (m_controller->analysis()->isRunning()) return;
        m_controller->document()->undoStack()->undo(); m_projectPanel->refreshAll();
    });
    m_redoAction = editMenu->addAction(tr("&Redo"), QKeySequence::Redo, this, [this] {
        if (m_controller->analysis()->isRunning()) return;
        m_controller->document()->undoStack()->redo(); m_projectPanel->refreshAll();
    });
    editMenu->addSeparator();
    editMenu->addAction(tr("&Backend Settings..."), this, [this] { m_projectPanel->navigateToField("backend"); });

    // ---- View ----
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    QAction *toggleProjectAction = viewMenu->addAction(tr("Project / Setup Panel"));
    toggleProjectAction->setCheckable(true);
    toggleProjectAction->setChecked(true);
    connect(toggleProjectAction, &QAction::toggled, m_projectPanel, &QWidget::setVisible);

    QAction *toggleInspectorAction = viewMenu->addAction(tr("Inspector Panel"));
    toggleInspectorAction->setCheckable(true);
    toggleInspectorAction->setChecked(true);
    connect(toggleInspectorAction, &QAction::toggled, m_inspectorPanel, &QWidget::setVisible);

    QAction *toggleProblemsAction = viewMenu->addAction(tr("Problems / Log Panel"));
    toggleProblemsAction->setCheckable(true);
    toggleProblemsAction->setChecked(true);
    connect(toggleProblemsAction, &QAction::toggled, m_problemsPanel, &QWidget::setVisible);

    viewMenu->addSeparator();
    viewMenu->addAction(tr("&Reset Layout"), this, &MainWindow::resetLayout);

    // ---- Model / Mesh / Analysis / Results ----
    QMenu *modelMenu = menuBar()->addMenu(tr("&Model"));
    modelMenu->addAction(tr("Show Model Tab"), this, [this] { m_centerTabs->setCurrentWidget(m_modelViewport); });
    m_drawAction = modelMenu->addAction(tr("Add Pressure Patch — Draw Rectangle"), this, &MainWindow::doDrawPatch);

    QMenu *meshMenu = menuBar()->addMenu(tr("Me&sh"));
    meshMenu->addAction(tr("Show Mesh Tab"), this, [this] { m_centerTabs->setCurrentWidget(m_meshViewport); });
    meshMenu->addAction(tr("Update Mesh"), this, [this] { rebuildMeshAndScene(); doValidate(); });

    QMenu *analysisMenu = menuBar()->addMenu(tr("&Analysis"));
    analysisMenu->addAction(tr("&Validate"), this, &MainWindow::doValidate);
    m_runAction = analysisMenu->addAction(tr("&Run Analysis"), this, &MainWindow::doRun);
    m_stopAction = analysisMenu->addAction(tr("&Stop Analysis"), this, &MainWindow::doStop);
    m_stopAction->setEnabled(false);

    QMenu *resultsMenu = menuBar()->addMenu(tr("&Results"));
    resultsMenu->addAction(tr("Show Results Tab"), this, [this] { m_centerTabs->setCurrentWidget(m_resultsViewport); });
    m_csvAction = resultsMenu->addAction(tr("Export Nodal Results CSV..."), this, &MainWindow::doExportResultsCsv);
    m_pngAction = resultsMenu->addAction(tr("Export Results PNG..."), this, &MainWindow::doExportResultsPng);

    // ---- Help ----
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, [this] {
        const auto backend = m_controller->document()->backendId();
        QMessageBox::about(this, tr("About FEM Analysis GUI"),
            tr("FEM Analysis GUI\n\nActive backend: %1\n"
               "Plate execution is available. C3D8 production execution arrives in phase C4.").arg(backend));
    });

    // ---- Toolbar ----
    QToolBar *toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);
    toolbar->addAction(m_newAction);
    toolbar->addAction(m_openAction);
    toolbar->addAction(m_saveAction);
    toolbar->addSeparator();
    toolbar->addAction(tr("Validate"), this, &MainWindow::doValidate);
    toolbar->addAction(tr("Update Mesh"), this, [this] { rebuildMeshAndScene(); doValidate(); });
    toolbar->addSeparator();
    toolbar->addAction(tr("Fit View"), this, [this] {
        if (auto *viewport = activeViewport())
            viewport->fitView();
    });
    toolbar->addSeparator();
    toolbar->addAction(m_runAction);
    toolbar->addAction(m_stopAction);
    toolbar->addSeparator();
    toolbar->addAction(tr("Export"), this, &MainWindow::doExportSolverJson);
}

void MainWindow::buildStatusBar()
{
    m_stateLabel = new QLabel(tr("Ready"), this);
    m_unitsLabel = new QLabel(tr("Units: mm - N - MPa"), this);
    m_nodesLabel = new QLabel(this);
    m_elementsLabel = new QLabel(this);
    m_problemsLabel = new QLabel(tr("0 errors, 0 warnings"), this);
    m_demoDataLabel = new QLabel(this);
    m_demoDataLabel->setStyleSheet("font-weight: bold; color: palette(link);");

    statusBar()->addWidget(m_stateLabel, 1);
    statusBar()->addPermanentWidget(m_problemsLabel);
    statusBar()->addPermanentWidget(m_unitsLabel);
    statusBar()->addPermanentWidget(m_nodesLabel);
    statusBar()->addPermanentWidget(m_elementsLabel);
    statusBar()->addPermanentWidget(m_demoDataLabel);
}

void MainWindow::connectDocumentSignals()
{
    ProjectDocument *doc = m_controller->document();
    connect(doc->undoStack(), &QUndoStack::canUndoChanged, this, &MainWindow::updateRunActionStates);
    connect(doc->undoStack(), &QUndoStack::canRedoChanged, this, &MainWindow::updateRunActionStates);
    connect(doc, &ProjectDocument::nameChanged, this, [this](const QString &name) {
        setWindowTitle(tr("FEM Analysis GUI — %1").arg(name));
    });
    connect(doc, &ProjectDocument::runsChanged, this, [this] {
        m_projectPanel->refreshAll();
        updateStatusBar();
        updateRunActionStates();
    });
    connect(doc, &ProjectDocument::modelChanged, this, &MainWindow::onModelEdited);
    connect(doc, &ProjectDocument::resultSetChanged, this, [this] {
        auto *document = m_controller->document();
        m_resultsViewport->setResultSet(document->hasResultSet() ? &document->resultSet() : nullptr);
        onSelectionCleared();
        updateStatusBar();
        updateRunActionStates();
    });
    setWindowTitle(tr("FEM Analysis GUI — %1").arg(doc->name()));
}

void MainWindow::connectAnalysisSignals()
{
    AnalysisController *analysis = m_controller->analysis();
    connect(analysis, &AnalysisController::logLine, this, [this](const QString &channel, const QString &line) {
        m_problemsPanel->appendSolverLog(channel == "stderr" ? "[stderr] " + line : line);
    });
    connect(analysis, &AnalysisController::runStarted, this, &MainWindow::onRunStarted);
    connect(analysis, &AnalysisController::runFinished, this, &MainWindow::onRunFinished);
    connect(analysis, &AnalysisController::backendConfigurationChanged, this, &MainWindow::updateRunActionStates);
    connect(analysis, &AnalysisController::phaseChanged, this, [this](const QString &phase, int completed, int total) {
        m_stateLabel->setText(completed >= 0 ? tr("%1: %2/%3").arg(phase).arg(completed).arg(total)
                                            : tr("%1 in progress...").arg(phase));
    });
}

ViewportWidget *MainWindow::activeViewport() const
{
    return qobject_cast<ViewportWidget *>(m_centerTabs->currentWidget());
}

void MainWindow::notImplemented(const QString &feature)
{
    m_stateLabel->setText(tr("%1 is not implemented in this build.").arg(feature));
    m_problemsPanel->appendOutput(tr("Action invoked (not implemented): %1").arg(feature));
}

// --------------------------------------------------------------- Project

bool MainWindow::confirmDiscardIfDirty()
{
    if (!m_controller->document()->isDirty())
        return true;

    QMessageBox::StandardButton choice = QMessageBox::question(
        this, tr("Unsaved changes"), tr("The current project has unsaved changes. Save before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);

    if (choice == QMessageBox::Cancel)
        return false;
    if (choice == QMessageBox::Discard)
        return true;

    return doSaveProject();
}

void MainWindow::doNewProject()
{
    if (m_controller->analysis()->isRunning()) return;
    if (!confirmDiscardIfDirty())
        return;
    m_controller->newProject();
}

void MainWindow::doOpenProject()
{
    if (m_controller->analysis()->isRunning()) return;
    if (!confirmDiscardIfDirty())
        return;
    QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), {}, tr("FEM Project (*.femproject.json)"));
    if (path.isEmpty())
        return;
    ProjectController::Result result = m_controller->open(path);
    if (!result.success)
        QMessageBox::warning(this, tr("Open Project"), result.error);
    else if (!result.error.isEmpty()) m_problemsPanel->appendOutput(result.error);
}

bool MainWindow::doSaveProject()
{
    if (m_controller->document()->filePath().isEmpty()) return doSaveProjectAs();
    ProjectController::Result result = m_controller->save();
    if (!result.success) {
        QMessageBox::warning(this, tr("Save Project"), result.error);
        return false;
    }
    m_stateLabel->setText(tr("Saved %1").arg(m_controller->document()->filePath()));
    return true;
}

bool MainWindow::doSaveProjectAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save Project As"), {}, tr("FEM Project (*.femproject.json)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".femproject.json"))
        path += ".femproject.json";
    ProjectController::Result result = m_controller->saveAs(path);
    if (!result.success) {
        QMessageBox::warning(this, tr("Save Project"), result.error);
        return false;
    }
    m_stateLabel->setText(tr("Saved %1").arg(path));
    return true;
}

void MainWindow::doExportSolverJson()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Export Solver JSON"), {}, tr("JSON (*.json)"));
    if (path.isEmpty())
        return;
    ProjectController::Result result = m_controller->exportSolverJson(path);
    if (!result.success) {
        QMessageBox::warning(this, tr("Export Solver JSON"), result.error);
        return;
    }
    m_problemsPanel->appendOutput(tr("Exported solver input to %1").arg(path));
}

void MainWindow::doOpenOutputFolder(const QString &path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// -------------------------------------------------------------- Analysis

void MainWindow::doValidate()
{
    m_lastIssues = m_controller->validate();
    m_problemsPanel->clearProblems();
    for (const ValidationIssue &issue : m_lastIssues) {
        ProblemsPanel::Severity severity = issue.severity == Severity::Error ? ProblemsPanel::Severity::Error
                                          : issue.severity == Severity::Warning ? ProblemsPanel::Severity::Warning
                                                                                 : ProblemsPanel::Severity::Information;
        m_problemsPanel->addProblem(severity, issue.message, issue.fieldKey);
    }
    m_projectPanel->setValidationIssues(m_lastIssues);
    m_inspectorPanel->showProjectSummary(m_controller->document(), m_lastIssues);
    updateRunActionStates();
    m_stateLabel->setText(hasErrors(m_lastIssues) ? tr("Validation found errors; see Problems panel.")
                                                   : tr("Validation passed."));
}

void MainWindow::doRun()
{
    if (m_controller->analysis()->isRunning())
        return;
    ValidationIssues issues = m_controller->analysis()->run();
    if (!issues.isEmpty()) {
        // run() only returns issues when it refused to start; surface them
        // the same way doValidate() does.
        m_lastIssues = issues;
        m_problemsPanel->clearProblems();
        for (const ValidationIssue &issue : m_lastIssues) {
            ProblemsPanel::Severity severity = issue.severity == Severity::Error ? ProblemsPanel::Severity::Error
                                              : issue.severity == Severity::Warning ? ProblemsPanel::Severity::Warning
                                                                                     : ProblemsPanel::Severity::Information;
            m_problemsPanel->addProblem(severity, issue.message, issue.fieldKey);
        }
        m_projectPanel->setValidationIssues(m_lastIssues);
        updateRunActionStates();
    }
}

void MainWindow::doStop()
{
    m_controller->analysis()->stop();
}

void MainWindow::onRunStarted(const QString &runId)
{
    m_projectPanel->setEnabled(false);
    updateRunActionStates();
    m_stateLabel->setText(tr("Running analysis (run %1)...").arg(runId));
    m_problemsPanel->appendSolverLog(tr("Run %1 started.").arg(runId));
}

void MainWindow::onRunFinished(const QString &runId, bool success, const QString &errorMessage)
{
    m_projectPanel->setEnabled(true);
    updateRunActionStates();
    if (m_closeAfterRun) {
        m_closeAfterRun = false;
        QTimer::singleShot(0, this, &QWidget::close);
    }

    if (!success) {
        bool cancelled = errorMessage.contains("Cancelled", Qt::CaseInsensitive);
        m_problemsPanel->addProblem(cancelled ? ProblemsPanel::Severity::Warning : ProblemsPanel::Severity::Error,
                                     tr("Run %1: %2").arg(runId, errorMessage), "run");
        m_stateLabel->setText(cancelled ? tr("Run cancelled.") : tr("Run failed."));
        updateStatusBar();
        return;
    }

    const RunRecord *run = m_controller->document()->findRun(runId);
    if (!run) {
        m_problemsPanel->addProblem(ProblemsPanel::Severity::Error, tr("Run %1 finished but its record is missing.").arg(runId), "run");
        return;
    }

    // AnalysisController publishes only fully validated results, on the GUI thread.
    m_centerTabs->setCurrentWidget(m_resultsViewport);
    m_problemsPanel->appendSolverLog(tr("Run %1 completed; results loaded.").arg(runId));
    m_stateLabel->setText(tr("Run completed."));
    updateStatusBar();
}

// ------------------------------------------------------------- Selection

void MainWindow::onNodePicked(vtkIdType solverNodeId)
{
    ProjectDocument *doc = m_controller->document();
    const bool fromResults = sender() == m_resultsViewport;
    const RunRecord *run = doc->hasResultSet() ? doc->findRun(doc->resultSet().runId()) : nullptr;
    if (fromResults && !run) return;
    auto mesh = doc->isPlate() ? PlateMeshBuilder::build(doc->plate()) : C3D8MeshBuilder::build(doc->solid());
    if (fromResults) mesh = doc->resultSet().grid();
    if (solverNodeId < 0 || solverNodeId >= mesh->GetNumberOfPoints()) { onSelectionCleared(); return; }
    const ResultSet *resultSet = doc->hasResultSet() && (fromResults || !doc->isResultSetStale()) ? &doc->resultSet() : nullptr;
    const auto &model = fromResults ? run->inputSnapshot : doc->model();
    const bool solid = std::holds_alternative<SolidDefinition>(model);
    const int nx = solid ? std::get<SolidDefinition>(model).nodeCountX() : std::get<PlateDefinition>(model).nodeCountX();
    const int nz = solid ? std::get<SolidDefinition>(model).nodeCountZ() : 1;
    InspectorPanel::NodeInfo info = buildNodeInfo(mesh.Get(), solverNodeId, nx, nz, solid, resultSet);
    info.resultContext = resultSet ? tr("Run %1%2").arg(resultSet->runId(), doc->isResultSetStale() ? tr(" (previous model revision)") : QString()) : QString();
    m_inspectorPanel->showNode(info);
    m_meshViewport->clearSelectionHighlight();
    m_resultsViewport->clearSelectionHighlight();
    if (!fromResults || !doc->isResultSetStale()) m_meshViewport->highlightNode(solverNodeId);
    if (fromResults || (doc->hasResultSet() && !doc->isResultSetStale())) m_resultsViewport->highlightNode(solverNodeId);
}

void MainWindow::onElementPicked(vtkIdType solverElementId)
{
    ProjectDocument *doc = m_controller->document();
    const bool fromResults = sender() == m_resultsViewport;
    const RunRecord *run = doc->hasResultSet() ? doc->findRun(doc->resultSet().runId()) : nullptr;
    if (fromResults && !run) return;
    auto mesh = doc->isPlate() ? PlateMeshBuilder::build(doc->plate()) : C3D8MeshBuilder::build(doc->solid());
    if (fromResults) mesh = doc->resultSet().grid();
    if (solverElementId < 0 || solverElementId >= mesh->GetNumberOfCells()) { onSelectionCleared(); return; }
    InspectorPanel::ElementInfo info = buildElementInfo(mesh.Get(), solverElementId, fromResults ? run->inputSnapshot : doc->model());
    info.resultContext = fromResults ? tr("Run %1%2").arg(run->runId, doc->isResultSetStale() ? tr(" (previous model revision)") : QString()) : QString();
    m_inspectorPanel->showElement(info);
    m_meshViewport->clearSelectionHighlight();
    m_resultsViewport->clearSelectionHighlight();
    if (!fromResults || !doc->isResultSetStale()) m_meshViewport->highlightElement(solverElementId);
    if (fromResults || (doc->hasResultSet() && !doc->isResultSetStale())) m_resultsViewport->highlightElement(solverElementId);
}

void MainWindow::onSelectionCleared()
{
    m_inspectorPanel->showProjectSummary(m_controller->document(), m_lastIssues);
    m_meshViewport->clearSelectionHighlight();
    m_resultsViewport->clearSelectionHighlight();
    m_modelViewport->clearSelectionHighlight();
}

// -------------------------------------------------------------------- UI

void MainWindow::onProjectReplaced()
{
    m_projectPanel->bindToController(m_controller);
    connectDocumentSignals();
    connectAnalysisSignals();
    m_resultsViewport->setResultSet(nullptr);
    m_projectPanel->setEnabled(true);
    m_problemsPanel->clearProblems();
    rebuildMeshAndScene();
    doValidate();
    updateStatusBar();
}

void MainWindow::onModelEdited()
{
    rebuildMeshAndScene();
    onSelectionCleared();
    m_projectPanel->refreshSummaries();
    doValidate();
    updateStatusBar();
}

void MainWindow::rebuildMeshAndScene()
{
    ProjectDocument *doc = m_controller->document();
    const double loadZ = doc->isSolid() && !doc->solid().zNodes.isEmpty() ? doc->solid().zNodes.first() : 0.0;
    auto mesh = doc->isPlate() ? PlateMeshBuilder::build(doc->plate()) : C3D8MeshBuilder::build(doc->solid());
    const auto &patches = doc->isPlate() ? doc->plate().patches : doc->solid().patches;
    m_modelViewport->setPlateMesh(mesh, loadZ);
    m_modelViewport->setPressurePatches(patches, loadZ);
    m_meshViewport->setPlateMesh(mesh, loadZ);
    m_meshViewport->clearSelectionHighlight();
}

void MainWindow::updateStatusBar()
{
    ProjectDocument *doc = m_controller->document();
    m_nodesLabel->setText(tr("Nodes: %1").arg(modelNodeCount(doc->model())));
    m_elementsLabel->setText(tr("Elements: %1").arg(modelElementCount(doc->model())));

    if (m_controller->analysis()->isRunning())
        m_demoDataLabel->setText(tr("Running..."));
    else if (doc->hasResultSet())
        m_demoDataLabel->setText(doc->isResultSetStale() ? tr("Results: stale (model edited since run)")
                                                          : tr("Results: run %1").arg(doc->resultSet().runId()));
    else
        m_demoDataLabel->setText(tr("No results yet"));
}

void MainWindow::updateRunActionStates()
{
    bool running = m_controller->analysis()->isRunning();
    m_runAction->setEnabled(!running && !hasErrors(m_controller->validate()) && m_controller->analysis()->backendFilesCheck().ok);
    m_stopAction->setEnabled(running);
    m_newAction->setEnabled(!running);
    m_openAction->setEnabled(!running);
    auto *doc = m_controller->document();
    m_undoAction->setEnabled(!running && doc->undoStack()->canUndo());
    m_redoAction->setEnabled(!running && doc->undoStack()->canRedo());
    m_drawAction->setEnabled(!running);
    m_modelViewport->setEditingEnabled(!running);
    m_csvAction->setEnabled(doc->hasResultSet());
    m_pngAction->setEnabled(doc->hasResultSet());
}

void MainWindow::doDrawPatch()
{
    if (m_controller->analysis()->isRunning()) return;
    m_centerTabs->setCurrentWidget(m_modelViewport);
    m_modelViewport->startRectangleDrawing();
}

void MainWindow::acceptDrawnPatch(const QRectF &bounds)
{
    if (m_controller->analysis()->isRunning()) return;
    auto *doc = m_controller->document();
    const auto domain = doc->isPlate() ? doc->plate() : PlateDefinition{doc->solid().xNodes, doc->solid().yNodes, {}, {}, doc->solid().patches};
    PressurePatch draft;
    draft.name = tr("Patch %1").arg(domain.patches.size() + 1);
    draft.x1 = bounds.left(); draft.x2 = bounds.right(); draft.y1 = bounds.top(); draft.y2 = bounds.bottom();
    PressurePatchDialog dialog(domain, draft, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto patch = dialog.patch();
    if (doc->isPlate()) doc->editPlate([&](auto &p) { p.patches.append(patch); }, tr("Draw pressure patch"));
    else doc->editSolid([&](auto &p) { p.patches.append(patch); }, tr("Draw pressure patch"));
    m_projectPanel->refreshAll();
    onPatchPicked(patch.id);
}

void MainWindow::onPatchPicked(const QUuid &id)
{
    m_centerTabs->setCurrentWidget(m_modelViewport);
    onSelectionCleared();
    const auto *doc = m_controller->document();
    const auto &patches = doc->isPlate() ? doc->plate().patches : doc->solid().patches;
    for (const auto &patch : patches) if (patch.id == id) {
        m_modelViewport->highlightPatch(id);
        m_projectPanel->selectPatch(id);
        m_inspectorPanel->showPatch(patch);
        return;
    }
}

void MainWindow::doExportResultsCsv()
{
    auto *doc = m_controller->document();
    if (!doc->hasResultSet()) return;
    QString path = QFileDialog::getSaveFileName(this, tr("Export nodal results"), {}, tr("CSV (*.csv)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".csv", Qt::CaseInsensitive)) path += ".csv";
    QString error;
    if (!ResultExporter::writeCsv(doc->resultSet(), doc->resultSetModelRevision(), doc->isResultSetStale(), path, &error))
        QMessageBox::warning(this, tr("CSV export"), error);
    else m_problemsPanel->appendOutput(tr("Nodal results exported to %1. Stress fields are arithmetic nodal averages.").arg(path));
}

void MainWindow::doExportResultsPng()
{
    auto *doc = m_controller->document();
    if (!doc->hasResultSet()) return;
    m_centerTabs->setCurrentWidget(m_resultsViewport);
    QString path = QFileDialog::getSaveFileName(this, tr("Export results view"), {}, tr("PNG (*.png)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
    QString error;
    if (!m_resultsViewport->exportScreenshot(path, doc->resultSetModelRevision(), doc->isResultSetStale(), &error))
        QMessageBox::warning(this, tr("PNG export"), error);
    else m_problemsPanel->appendOutput(tr("Results view exported to %1.").arg(path));
}

void MainWindow::saveLayout()
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("mainSplitter", m_mainSplitter->saveState());
    settings.setValue("verticalSplitter", m_verticalSplitter->saveState());
    settings.endGroup();
}

void MainWindow::restoreLayout()
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    if (settings.contains("geometry"))
        restoreGeometry(settings.value("geometry").toByteArray());
    if (settings.contains("mainSplitter"))
        m_mainSplitter->restoreState(settings.value("mainSplitter").toByteArray());
    else
        m_mainSplitter->setSizes({200, 600, 200});
    if (settings.contains("verticalSplitter"))
        m_verticalSplitter->restoreState(settings.value("verticalSplitter").toByteArray());
    else
        m_verticalSplitter->setSizes({700, 200});
    settings.endGroup();
}

void MainWindow::resetLayout()
{
    resize(kDefaultWindowWidth, kDefaultWindowHeight);
    m_mainSplitter->setSizes({200, 600, 200});
    m_verticalSplitter->setSizes({700, 200});
    m_projectPanel->setVisible(true);
    m_inspectorPanel->setVisible(true);
    m_problemsPanel->setVisible(true);
    m_stateLabel->setText(tr("Layout reset to default."));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_controller->analysis()->isRunning()) {
        QMessageBox::StandardButton choice = QMessageBox::question(
            this, tr("Analysis running"), tr("A solver run is in progress. Stop it and close?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (choice != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_controller->analysis()->stop();
        m_closeAfterRun = true;
        event->ignore();
        return; // finished() will schedule close again, after manifest/log finalization
    }

    if (!confirmDiscardIfDirty()) {
        event->ignore();
        return;
    }

    saveLayout();
    QMainWindow::closeEvent(event);
}

} // namespace femgui
