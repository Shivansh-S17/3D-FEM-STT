#include "ui/MainWindow.h"

#include "ui/InspectorPanel.h"
#include "ui/ProblemsPanel.h"
#include "ui/ProjectPanel.h"
#include "ui/ViewportWidget.h"

#include <QAction>
#include <QCloseEvent>
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

namespace femgui {

namespace {
constexpr int kDefaultWindowWidth = 1400;
constexpr int kDefaultWindowHeight = 900;
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("C3D8 Solid Analysis — Input Preview"));
    resize(kDefaultWindowWidth, kDefaultWindowHeight);

    buildCentralWidget();
    buildMenusAndToolbar();
    buildStatusBar();
    wireDemoBehavior();

    restoreLayout();

    statusBar()->showMessage(tr("Ready"), 3000);
    m_problemsPanel->appendOutput(tr("C3D8 input preview started. A reduced sample slab mesh is displayed."));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildCentralWidget()
{
    m_projectPanel = new ProjectPanel(this);

    m_centerTabs = new QTabWidget(this);
    m_centerTabs->addTab(new ViewportWidget(ViewportWidget::ViewMode::Model, this), tr("Model"));
    m_centerTabs->addTab(new ViewportWidget(ViewportWidget::ViewMode::Mesh, this), tr("Mesh"));
    m_centerTabs->addTab(new ViewportWidget(ViewportWidget::ViewMode::Results, this), tr("Results"));

    m_inspectorPanel = new InspectorPanel(this);
    m_problemsPanel = new ProblemsPanel(this);

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
    fileMenu->addAction(tr("&New Project"), QKeySequence::New, this, [this] { notImplemented(tr("New Project")); });
    fileMenu->addAction(tr("&Open Project..."), QKeySequence::Open, this, [this] { notImplemented(tr("Open Project")); });
    fileMenu->addMenu(tr("Open &Recent"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Save"), QKeySequence::Save, this, [this] { notImplemented(tr("Save")); });
    fileMenu->addAction(tr("Save &As..."), QKeySequence::SaveAs, this, [this] { notImplemented(tr("Save As")); });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Import &Geometry or Mesh..."), this, [this] { notImplemented(tr("Import Geometry/Mesh")); });
    fileMenu->addAction(tr("Import C3D8 Input &JSON..."), this, [this] { notImplemented(tr("Import C3D8 Input JSON")); });
    fileMenu->addAction(tr("Import &Results VTK..."), this, [this] { notImplemented(tr("Import Results VTK")); });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Export C3D8 Input J&SON..."), this, [this] { notImplemented(tr("Export C3D8 Input JSON")); });
    fileMenu->addAction(tr("Export &Screenshot..."), this, [this] { notImplemented(tr("Export Screenshot")); });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    // ---- Edit ----
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(tr("&Undo"), QKeySequence::Undo, this, [this] { notImplemented(tr("Undo")); });
    editMenu->addAction(tr("&Redo"), QKeySequence::Redo, this, [this] { notImplemented(tr("Redo")); });
    editMenu->addSeparator();
    editMenu->addAction(tr("&Delete Selected Definition"), QKeySequence::Delete, this, [this] { notImplemented(tr("Delete")); });
    editMenu->addSeparator();
    editMenu->addAction(tr("&Preferences..."), this, [this] { notImplemented(tr("Preferences")); });

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
    viewMenu->addSeparator();

    QMenu *cameraMenu = viewMenu->addMenu(tr("Camera Views"));
    for (const QString &preset : {tr("Isometric"), tr("Front"), tr("Top"), tr("Right")}) {
        cameraMenu->addAction(preset, this, [this, preset] {
            if (auto *viewport = activeViewport())
                viewport->setCameraPreset(preset);
        });
    }

    QMenu *displayMenu = viewMenu->addMenu(tr("Display Mode"));
    for (const QString &mode : {tr("Surface"), tr("Surface with Edges"), tr("Wireframe"), tr("Points")}) {
        displayMenu->addAction(mode, this, [this, mode] {
            if (auto *viewport = activeViewport())
                viewport->setRepresentation(mode);
        });
    }

    QMenu *themeMenu = viewMenu->addMenu(tr("Theme"));
    themeMenu->addAction(tr("System Default"), this, [this] { notImplemented(tr("Theme switching")); });

    // ---- Model / Mesh / Analysis / Results ----
    QMenu *modelMenu = menuBar()->addMenu(tr("&Model"));
    modelMenu->addAction(tr("X / Y / Z Coordinates..."), this, [this] { notImplemented(tr("Edit C3D8 coordinates")); });
    modelMenu->addAction(tr("Material (Emod, nu)..."), this, [this] { notImplemented(tr("Edit C3D8 material")); });
    modelMenu->addAction(tr("Subgrade (Kx, Ky, Kz)..."), this, [this] { notImplemented(tr("Edit C3D8 subgrade")); });
    modelMenu->addAction(tr("Pressure Rectangles..."), this, [this] { notImplemented(tr("Edit C3D8 pressure rectangles")); });

    QMenu *meshMenu = menuBar()->addMenu(tr("Me&sh"));
    meshMenu->addAction(tr("View Sample C3D8 Mesh"), this, [this] { m_centerTabs->setCurrentIndex(1); });

    QMenu *analysisMenu = menuBar()->addMenu(tr("&Analysis"));
    m_runAction = analysisMenu->addAction(tr("&Run Analysis"), this, [this] { notImplemented(tr("Run Analysis")); });
    m_runAction->setEnabled(false); // stays disabled until validation passes
    m_stopAction = analysisMenu->addAction(tr("&Stop Analysis"), this, [this] { notImplemented(tr("Stop Analysis")); });
    m_stopAction->setEnabled(false); // only available while a job is running
    analysisMenu->addSeparator();
    analysisMenu->addAction(tr("&Validate"), this, [this] { notImplemented(tr("Validate")); });

    QMenu *resultsMenu = menuBar()->addMenu(tr("&Results"));
    resultsMenu->addAction(tr("Displacement"), this, [this] { m_centerTabs->setCurrentIndex(2); });
    resultsMenu->addAction(tr("Stress"), this, [this] { m_centerTabs->setCurrentIndex(2); });
    resultsMenu->addAction(tr("Strain"), this, [this] { m_centerTabs->setCurrentIndex(2); });

    // ---- Help ----
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, [this] {
        QMessageBox::about(this, tr("About FEM Analysis GUI"),
            tr("C3D8 Solid Analysis — Input Preview\n\n"
               "Panels reflect the received C3D8 input schema.\n"
               "Parameter values are references; the Results tab shows preview geometry."));
    });

    // ---- Toolbar ----
    QToolBar *toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);
    toolbar->addAction(tr("New"), this, [this] { notImplemented(tr("New Project")); });
    toolbar->addAction(tr("Open"), this, [this] { notImplemented(tr("Open Project")); });
    toolbar->addAction(tr("Save"), this, [this] { notImplemented(tr("Save")); });
    toolbar->addSeparator();
    toolbar->addAction(tr("Undo"), this, [this] { notImplemented(tr("Undo")); });
    toolbar->addAction(tr("Redo"), this, [this] { notImplemented(tr("Redo")); });
    toolbar->addSeparator();
    toolbar->addAction(tr("Fit View"), this, [this] {
        if (auto *viewport = activeViewport())
            viewport->fitView();
    });
    toolbar->addAction(tr("Select"), this, [this] { notImplemented(tr("Selection tool")); });
    toolbar->addAction(tr("Mesh"), this, [this] { m_centerTabs->setCurrentIndex(1); });
    toolbar->addSeparator();
    toolbar->addAction(m_runAction);
    toolbar->addAction(m_stopAction);
    toolbar->addSeparator();
    toolbar->addAction(tr("Export"), this, [this] { notImplemented(tr("Export")); });
}

void MainWindow::buildStatusBar()
{
    m_stateLabel = new QLabel(tr("Ready"), this);
    m_unitsLabel = new QLabel(tr("Units: under review"), this);
    m_nodesLabel = new QLabel(tr("Nodes: 27"), this);
    m_elementsLabel = new QLabel(tr("C3D8 bricks: 8"), this);
    m_problemsLabel = new QLabel(tr("0 errors, 0 warnings"), this);
    m_demoDataLabel = new QLabel(tr("Demo data"), this);
    m_demoDataLabel->setStyleSheet("font-weight: bold; color: palette(link);");

    statusBar()->addWidget(m_stateLabel, 1);
    statusBar()->addPermanentWidget(m_problemsLabel);
    statusBar()->addPermanentWidget(m_unitsLabel);
    statusBar()->addPermanentWidget(m_nodesLabel);
    statusBar()->addPermanentWidget(m_elementsLabel);
    statusBar()->addPermanentWidget(m_demoDataLabel);
}

void MainWindow::wireDemoBehavior()
{
    connect(m_projectPanel, &ProjectPanel::statusMessage, this, [this](const QString &message) {
        m_stateLabel->setText(message);
    });

    connect(m_problemsPanel, &ProblemsPanel::problemCountsChanged, this, [this](int errors, int warnings) {
        m_problemsLabel->setText(tr("%1 errors, %2 warnings").arg(errors).arg(warnings));
    });

    connect(m_problemsPanel, &ProblemsPanel::navigateToField, this, [this](const QString &sourceField) {
        if (!sourceField.isEmpty())
            m_stateLabel->setText(tr("Navigate to: %1").arg(sourceField));
    });

    // Seed the Problems panel with a couple of demonstration entries so the
    // panel's severity handling and double-click navigation can be exercised.
    m_problemsPanel->addProblem(ProblemsPanel::Severity::Warning,
        tr("C3D8 input values are preview examples; editing and solve are not connected."), tr("C3D8 solid model"));
    m_problemsPanel->addProblem(ProblemsPanel::Severity::Information,
        tr("Sample structured slab mesh generated for demonstration."), {});
    m_problemsPanel->appendSolverLog(tr("C3D8 solver not connected in this milestone. Run Analysis is disabled."));
}

ViewportWidget *MainWindow::activeViewport() const
{
    return qobject_cast<ViewportWidget *>(m_centerTabs->currentWidget());
}

void MainWindow::notImplemented(const QString &feature)
{
    m_stateLabel->setText(tr("%1 is not implemented in this prototype.").arg(feature));
    m_problemsPanel->appendOutput(tr("Action invoked (prototype stub): %1").arg(feature));
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
        m_mainSplitter->setSizes({200, 600, 200}); // approximate 1:3:1
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
    saveLayout();
    QMainWindow::closeEvent(event);
}

} // namespace femgui
