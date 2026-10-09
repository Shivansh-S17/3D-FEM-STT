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
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QStyle>
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
    setWindowTitle(tr("FEM Analysis GUI"));
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

    m_problemsPanel->appendOutput(tr("FEM Analysis GUI started. Backend: %1.").arg(m_controller->document()->backendId()));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildCentralWidget()
{
    m_projectPanel = new ProjectPanel(this);
    connect(m_projectPanel, &ProjectPanel::statusMessage, this, [this](const QString &message) {
        m_stateLabel->setText(message);
    });
    connect(m_projectPanel, &ProjectPanel::openOutputFolderRequested, this, &MainWindow::doOpenOutputFolder);
    connect(m_projectPanel, &ProjectPanel::runRequested, this, &MainWindow::doRun);
    connect(m_projectPanel, &ProjectPanel::stopRequested, this, &MainWindow::doStop);
    connect(m_projectPanel, &ProjectPanel::showResultsRequested, this, [this] {
        m_centerTabs->setCurrentWidget(m_resultsViewport);
    });
    connect(m_projectPanel, &ProjectPanel::exportResultsCsvRequested, this, &MainWindow::doExportResultsCsv);
    connect(m_projectPanel, &ProjectPanel::exportResultsImageRequested, this, &MainWindow::doExportResultsPng);
    connect(m_projectPanel, &ProjectPanel::loadRunRequested, this, [this](const QString &runId) {
        auto result = m_controller->loadRunResults(runId);
        if (!result.success) m_problemsPanel->addProblem(ProblemsPanel::Severity::Error, result.error, "run");
        else { m_centerTabs->setCurrentWidget(m_resultsViewport); m_projectPanel->refreshAll(); updateStatusBar(); }
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
    m_centerTabs->setDocumentMode(true);
    m_centerTabs->addTab(m_modelViewport, tr("Model"));
    m_centerTabs->addTab(m_meshViewport, tr("Mesh"));
    m_centerTabs->addTab(m_resultsViewport, tr("Results"));
    connect(m_centerTabs, &QTabWidget::currentChanged, this, &MainWindow::onCenterTabChanged);
    connect(m_projectPanel, &ProjectPanel::workflowStepChanged, this, &MainWindow::onWorkflowStepChanged);

    m_inspectorPanel = new InspectorPanel(this);
    m_problemsPanel = new ProblemsPanel(this);
    connect(m_problemsPanel, &ProblemsPanel::navigateToField, this, [this](const QString &fieldKey) {
        m_projectPanel->navigateToField(fieldKey);
    });
    connect(m_problemsPanel, &ProblemsPanel::problemCountsChanged, this, [this](int errors, int warnings) {
        m_problemsLabel->setText(tr("%1 errors, %2 warnings").arg(errors).arg(warnings));
    });

    m_centerTabs->setMinimumWidth(400);
    setCentralWidget(m_centerTabs);

    // Advanced dock island capabilities (VS Code style)
    setDockNestingEnabled(true);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);

    // Configure corners so the Left dock widget area spans the full vertical height (top to bottom),
    // matching the reference image where the left workflow panel takes all vertical space
    // and the bottom logs panel sits underneath the center and right panels.
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);

    // 1. Left Dock: Project Workflow
    m_projectDock = new QDockWidget(tr("Project Workflow"), this);
    m_projectDock->setObjectName(QStringLiteral("ProjectDock"));
    m_projectDock->setWidget(m_projectPanel);
    m_projectDock->setMinimumWidth(280);
    m_projectDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::LeftDockWidgetArea, m_projectDock);

    // 2. Right Dock: Inspector & General Info
    m_inspectorDock = new QDockWidget(tr("Inspector & General Info"), this);
    m_inspectorDock->setObjectName(QStringLiteral("InspectorDock"));
    m_inspectorDock->setWidget(m_inspectorPanel);
    m_inspectorDock->setMinimumWidth(250);
    m_inspectorDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);

    // 3. Bottom Dock: Problems & Logs
    m_problemsDock = new QDockWidget(tr("Problems & Logs"), this);
    m_problemsDock->setObjectName(QStringLiteral("ProblemsDock"));
    m_problemsDock->setWidget(m_problemsPanel);
    m_problemsDock->setMinimumHeight(130);
    m_problemsDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::BottomDockWidgetArea, m_problemsDock);
}

void MainWindow::buildMenusAndToolbar()
{
    // ---- File ----
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    m_newAction = fileMenu->addAction(tr("&New C3D8 Project"), QKeySequence::New, this, &MainWindow::doNewProject);
    m_openAction = fileMenu->addAction(tr("&Open Project..."), QKeySequence::Open, this, &MainWindow::doOpenProject);
    m_importAction = fileMenu->addAction(tr("&Import C3D8 Solver Input..."), this, &MainWindow::doImportC3D8Input);
    fileMenu->addSeparator();
    m_saveAction = fileMenu->addAction(tr("&Save"), QKeySequence::Save, this, [this] { doSaveProject(); });
    fileMenu->addAction(tr("Save &As..."), QKeySequence::SaveAs, this, [this] { doSaveProjectAs(); });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Export Solver &JSON..."), this, &MainWindow::doExportSolverJson);
    m_newAction->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
    m_openAction->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    m_importAction->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    m_saveAction->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_newAction->setStatusTip(tr("Create a fresh structured C3D8 project"));
    m_openAction->setStatusTip(tr("Open a saved FEM project"));
    m_importAction->setStatusTip(tr("Import the C3D8 solver JSON input format"));
    m_saveAction->setStatusTip(tr("Save the current project and run references"));
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
    m_undoAction->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    m_redoAction->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    m_undoAction->setStatusTip(tr("Undo the last model modification"));
    m_redoAction->setStatusTip(tr("Redo the last undone modification"));
    editMenu->addSeparator();
    editMenu->addAction(tr("&Backend Settings..."), this, [this] { m_projectPanel->navigateToField("backend"); });

    // ---- View ----
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    QAction *toggleProjectAction = m_projectDock->toggleViewAction();
    toggleProjectAction->setText(tr("Project Workflow Panel"));
    viewMenu->addAction(toggleProjectAction);

    QAction *toggleInspectorAction = m_inspectorDock->toggleViewAction();
    toggleInspectorAction->setText(tr("Inspector Panel"));
    viewMenu->addAction(toggleInspectorAction);

    QAction *toggleProblemsAction = m_problemsDock->toggleViewAction();
    toggleProblemsAction->setText(tr("Problems & Logs Panel"));
    viewMenu->addAction(toggleProblemsAction);

    viewMenu->addSeparator();
    m_fitViewAction = viewMenu->addAction(tr("&Fit View"), this, &MainWindow::doFitCurrentView);
    m_fitViewAction->setShortcut(QKeySequence(Qt::Key_F));
    m_fitViewAction->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    m_fitViewAction->setStatusTip(tr("Fit the active 3D view to the window (F)"));
    viewMenu->addAction(tr("&Reset Layout"), this, &MainWindow::resetLayout);

    viewMenu->addSeparator();
    QMenu *themeMenu = viewMenu->addMenu(tr("&Theme"));
    m_themeGroup = new QActionGroup(this);
    m_themeGroup->setExclusive(true);

    auto addThemeAction = [this, themeMenu](const QString &text, UiStyle::Theme theme) {
        QAction *act = themeMenu->addAction(text);
        act->setCheckable(true);
        m_themeGroup->addAction(act);
        if (UiStyle::currentTheme() == theme)
            act->setChecked(true);
        connect(act, &QAction::triggered, this, [this, theme] {
            onThemeChanged(theme);
        });
        return act;
    };

    m_themeGlassDarkAction = addThemeAction(tr("Liquid Glass &Dark (Recommended)"), UiStyle::Theme::LiquidGlassDark);
    m_themeGlassLightAction = addThemeAction(tr("Liquid Glass &Light"), UiStyle::Theme::LiquidGlassLight);
    m_themeClassicAction = addThemeAction(tr("&Classic System"), UiStyle::Theme::ClassicSystem);

    // ---- Model / Mesh / Analysis / Results ----
    QMenu *modelMenu = menuBar()->addMenu(tr("&Model"));
    modelMenu->addAction(tr("Show Model Tab"), this, [this] { m_centerTabs->setCurrentWidget(m_modelViewport); });
    m_drawAction = modelMenu->addAction(tr("Add Pressure Patch — Draw Rectangle"), this, &MainWindow::doDrawPatch);

    QMenu *meshMenu = menuBar()->addMenu(tr("Me&sh"));
    meshMenu->addAction(tr("Show Mesh Tab"), this, [this] { m_centerTabs->setCurrentWidget(m_meshViewport); });

    QMenu *analysisMenu = menuBar()->addMenu(tr("&Analysis"));
    m_validateAction = analysisMenu->addAction(tr("&Validate"), this, &MainWindow::doValidate);
    m_validateAction->setShortcut(QKeySequence(Qt::Key_F5));
    m_validateAction->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    m_validateAction->setStatusTip(tr("Validate the current model and solver configuration (F5)"));
    m_runAction = analysisMenu->addAction(tr("&Run Analysis"), this, &MainWindow::doRun);
    m_stopAction = analysisMenu->addAction(tr("&Stop Analysis"), this, &MainWindow::doStop);
    m_runAction->setShortcut(QKeySequence("Ctrl+R"));
    m_runAction->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_stopAction->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_runAction->setStatusTip(tr("Run the validated model (Ctrl+R)"));
    m_stopAction->setStatusTip(tr("Request cancellation of the active solver run"));
    m_stopAction->setEnabled(false);

    QMenu *resultsMenu = menuBar()->addMenu(tr("&Results"));
    resultsMenu->addAction(tr("Show Results Tab"), this, [this] { m_centerTabs->setCurrentWidget(m_resultsViewport); });
    m_csvAction = resultsMenu->addAction(tr("Export Results &CSV..."), this, &MainWindow::doExportResultsCsv);
    m_pngAction = resultsMenu->addAction(tr("Export Results &Image..."), this, &MainWindow::doExportResultsPng);

    // ---- Help ----
    helpMenuSetup:
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("C3D8 &Workflow"), this, [this] {
        QMessageBox::information(this, tr("C3D8 Workflow"),
            tr("1. Start a new C3D8 project, import solver JSON, or open a saved project from Overview.\n"
               "2. Follow Model, Material & Foundation, and Loads in the numbered setup panel.\n"
               "3. Open Validate & Run, resolve any linked problems, then select Run Analysis.\n"
               "4. Inspect fields, sections, deformation, extrema, and selections under Results.\n"
               "5. Save the project to retain the model and run references.\n\n"
               "Shortcuts: F5 validates; Ctrl+R runs. Solver paths and dependency checks are under Advanced.\n\n"
               "C3D8 runs must pass residual, stiffness-symmetry, force-balance, and moment-balance gates."));
    });
    helpMenu->addAction(tr("&About"), this, [this] {
        const auto backend = m_controller->document()->backendId();
        QMessageBox::about(this, tr("About FEM Analysis GUI"),
            tr("FEM Analysis GUI 0.2.0\n\nActive backend: %1\n"
               "Plate and C3D8 execution are available. C3D8 engineering validation remains provisional.").arg(backend));
    });

    // ---- Toolbar ----
    QToolBar *toolbar = addToolBar(tr("Main"));
    toolbar->setObjectName(QStringLiteral("MainToolbar"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(m_newAction);
    toolbar->addAction(m_openAction);
    toolbar->addAction(m_saveAction);
    toolbar->addSeparator();
    toolbar->addAction(m_undoAction);
    toolbar->addAction(m_redoAction);
}

void MainWindow::buildStatusBar()
{
    m_stateLabel = new QLabel(tr("Ready"), this);
    m_problemsLabel = new QLabel(tr("0 errors, 0 warnings"), this);
    m_problemsLabel->setCursor(Qt::PointingHandCursor);
    m_problemsLabel->setToolTip(tr("Click to toggle Problems panel"));
    m_problemsLabel->installEventFilter(this);
    m_demoDataLabel = new QLabel(this);
    m_demoDataLabel->setProperty("state", "success");
    m_progressBar = new QProgressBar(this);
    m_progressBar->setMaximumWidth(180);
    m_progressBar->setTextVisible(false);
    m_progressBar->hide();

    statusBar()->addWidget(m_stateLabel, 1);
    statusBar()->addPermanentWidget(m_progressBar);
    statusBar()->addPermanentWidget(m_problemsLabel);
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
        const QMap<QString, QString> names{{"validation", tr("Preparing input")},
                                           {"assembly", tr("Assembling system")},
                                           {"solid-assembly", tr("Assembling solid stiffness")},
                                           {"foundation", tr("Applying foundation")},
                                           {"foundation-assembly", tr("Applying foundation")},
                                           {"loading", tr("Applying loads")},
                                           {"loads", tr("Applying loads")},
                                           {"load-assembly", tr("Applying pressure loads")},
                                           {"solve", tr("Solving system")},
                                           {"recovery", tr("Recovering results")},
                                           {"postprocessing", tr("Recovering results")},
                                           {"export", tr("Publishing results")},
                                           {"import", tr("Importing results")}};
        const QString display = names.value(phase.toLower(), phase);
        m_stateLabel->setText(completed >= 0 ? tr("%1 — %2 of %3").arg(display).arg(completed).arg(total)
                                            : tr("%1…").arg(display));
        m_progressBar->show();
        if (completed >= 0 && total > 0) { m_progressBar->setRange(0, total); m_progressBar->setValue(completed); }
        else m_progressBar->setRange(0, 0);
    });
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
    m_controller->newProject(ModelKind::SolidC3D8);
}

void MainWindow::doImportC3D8Input()
{
    if (m_controller->analysis()->isRunning() || !confirmDiscardIfDirty()) return;
    const auto path = QFileDialog::getOpenFileName(this, tr("Import C3D8 Solver Input"), {},
                                                    tr("C3D8 solver input (*.json);;All files (*)"));
    if (path.isEmpty()) return;
    const auto result = m_controller->importC3D8SolverInput(path);
    if (!result.success) {
        QMessageBox::warning(this, tr("Import C3D8 Solver Input"), result.error);
        return;
    }
    m_stateLabel->setText(tr("Imported C3D8 input from %1; save it as a project to keep changes.").arg(path));
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
    if (hasErrors(m_lastIssues)) {
        m_problemsPanel->setVisible(true);
        m_stateLabel->setText(tr("Validation found errors; see Problems panel."));
    } else {
        m_stateLabel->setText(tr("Validation passed."));
    }
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
        m_problemsPanel->setVisible(true);
        updateRunActionStates();
    }
}

void MainWindow::doStop()
{
    m_controller->analysis()->stop();
}

void MainWindow::doFitCurrentView()
{
    if (m_centerTabs->currentWidget() == m_modelViewport) m_modelViewport->fitView();
    else if (m_centerTabs->currentWidget() == m_meshViewport) m_meshViewport->fitView();
    else if (m_centerTabs->currentWidget() == m_resultsViewport) m_resultsViewport->fitView();
}

void MainWindow::onCenterTabChanged(int index)
{
    m_modelViewport->cancelRectangleDrawing();
    onSelectionCleared();
    if (m_syncingNav) return;
    m_syncingNav = true;
    if (index == 0) {
        m_projectPanel->navigateToField("coordinates");
    } else if (index == 1) {
        m_projectPanel->navigateToField("mesh");
    } else if (index == 2) {
        m_projectPanel->navigateToField("results");
    }
    m_syncingNav = false;
}

void MainWindow::onWorkflowStepChanged(int pageIndex)
{
    if (m_syncingNav) return;
    m_syncingNav = true;
    // kPageOverview = 0, kPageMaterial = 1, kPageFoundation = 2, kPageCoordinates = 3,
    // kPageMesh = 4, kPageLoads = 5, kPageAnalysis = 6, kPageResults = 7, kPageAdvanced = 8
    if (pageIndex == 4) { // Mesh
        m_centerTabs->setCurrentWidget(m_meshViewport);
    } else if (pageIndex == 7) { // Results
        m_centerTabs->setCurrentWidget(m_resultsViewport);
    } else if (pageIndex == 0 || pageIndex == 1 || pageIndex == 2 || pageIndex == 3 || pageIndex == 5) {
        m_centerTabs->setCurrentWidget(m_modelViewport);
    }
    m_syncingNav = false;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_problemsLabel && event->type() == QEvent::MouseButtonRelease) {
        if (m_problemsDock)
            m_problemsDock->setVisible(!m_problemsDock->isVisible());
        else if (m_problemsPanel)
            m_problemsPanel->setVisible(!m_problemsPanel->isVisible());
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onRunStarted(const QString &runId)
{
    m_projectPanel->setEnabled(false);
    updateRunActionStates();
    m_stateLabel->setText(tr("Running analysis (run %1)...").arg(runId));
    m_progressBar->setRange(0, 0);
    m_progressBar->show();
    m_problemsPanel->appendSolverLog(tr("Run %1 started.").arg(runId));
}

void MainWindow::onRunFinished(const QString &runId, bool success, const QString &errorMessage)
{
    m_projectPanel->setEnabled(true);
    updateRunActionStates();
    m_progressBar->hide();
    if (m_closeAfterRun) {
        m_closeAfterRun = false;
        QTimer::singleShot(0, this, &QWidget::close);
    }

    if (!success) {
        bool cancelled = errorMessage.contains("Cancelled", Qt::CaseInsensitive);
        m_problemsPanel->addProblem(cancelled ? ProblemsPanel::Severity::Warning : ProblemsPanel::Severity::Error,
                                     tr("Run %1: %2").arg(runId, errorMessage), "run");
        m_problemsPanel->setVisible(true);
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
    m_projectPanel->navigateToField("results");
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
    const char *state = "";
    if (m_controller->analysis()->isRunning()) {
        m_demoDataLabel->setText(tr("Running..."));
        state = "warning";
    } else if (doc->hasResultSet()) {
        if (doc->isResultSetStale()) {
            m_demoDataLabel->setText(tr("Results: previous revision"));
            state = "warning";
        } else {
            m_demoDataLabel->setText(tr("Results: current"));
            state = "success";
        }
    } else {
        m_demoDataLabel->setText(tr("No results yet"));
    }
    m_demoDataLabel->setProperty("state", state);
    m_demoDataLabel->style()->unpolish(m_demoDataLabel);
    m_demoDataLabel->style()->polish(m_demoDataLabel);
}

void MainWindow::updateRunActionStates()
{
    bool running = m_controller->analysis()->isRunning();
    const bool canRun = !running && !hasErrors(m_controller->validate()) && m_controller->analysis()->backendFilesCheck().ok;
    if (m_validateAction) m_validateAction->setEnabled(!running);
    if (m_fitViewAction) m_fitViewAction->setEnabled(true);
    m_runAction->setEnabled(canRun);
    m_stopAction->setEnabled(running);
    m_stopAction->setVisible(running);
    m_newAction->setEnabled(!running);
    m_openAction->setEnabled(!running);
    m_importAction->setEnabled(!running);
    m_saveAction->setEnabled(!running);
    auto *doc = m_controller->document();
    m_undoAction->setEnabled(!running && doc->undoStack()->canUndo());
    m_redoAction->setEnabled(!running && doc->undoStack()->canRedo());
    m_drawAction->setEnabled(!running);
    m_modelViewport->setEditingEnabled(!running);
    m_csvAction->setEnabled(doc->hasResultSet());
    m_pngAction->setEnabled(doc->hasResultSet());
    m_projectPanel->setAnalysisActionState(canRun, running);
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
    settings.setValue("windowState", saveState());
    settings.endGroup();
}

void MainWindow::restoreLayout()
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    if (settings.contains("geometry"))
        restoreGeometry(settings.value("geometry").toByteArray());
    if (settings.contains("windowState"))
        restoreState(settings.value("windowState").toByteArray());
    settings.endGroup();
}

void MainWindow::resetLayout()
{
    resize(kDefaultWindowWidth, kDefaultWindowHeight);
    if (m_projectDock) {
        addDockWidget(Qt::LeftDockWidgetArea, m_projectDock);
        m_projectDock->setFloating(false);
        m_projectDock->setVisible(true);
    }
    if (m_inspectorDock) {
        addDockWidget(Qt::RightDockWidgetArea, m_inspectorDock);
        m_inspectorDock->setFloating(false);
        m_inspectorDock->setVisible(true);
    }
    if (m_problemsDock) {
        addDockWidget(Qt::BottomDockWidgetArea, m_problemsDock);
        m_problemsDock->setFloating(false);
        m_problemsDock->setVisible(true);
    }
    m_stateLabel->setText(tr("Layout reset to default."));
}

void MainWindow::onThemeChanged(UiStyle::Theme theme)
{
    if (auto *app = qobject_cast<QApplication *>(QApplication::instance())) {
        UiStyle::setTheme(*app, theme);
    }

    if (theme == UiStyle::Theme::LiquidGlassDark && m_themeGlassDarkAction) {
        m_themeGlassDarkAction->setChecked(true);
    } else if (theme == UiStyle::Theme::LiquidGlassLight && m_themeGlassLightAction) {
        m_themeGlassLightAction->setChecked(true);
    } else if (theme == UiStyle::Theme::ClassicSystem && m_themeClassicAction) {
        m_themeClassicAction->setChecked(true);
    }

    if (m_modelViewport) m_modelViewport->refreshThemeColors();
    if (m_meshViewport) m_meshViewport->refreshThemeColors();
    if (m_resultsViewport) m_resultsViewport->refreshThemeColors();

    if (m_stateLabel) {
        m_stateLabel->setText(tr("Theme applied: %1").arg(UiStyle::themeName(theme)));
    }
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
