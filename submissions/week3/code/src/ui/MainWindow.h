#pragma once

#include <QMainWindow>
#include <QRectF>
#include <QUuid>

#include <vtkType.h>

#include "core/ValidationIssue.h"
#include "ui/UiStyle.h"

class QSplitter;
class QTabWidget;
class QLabel;
class QAction;
class QActionGroup;
class QProgressBar;
class QDockWidget;

namespace femgui {

class ProjectPanel;
class ViewportWidget;
class InspectorPanel;
class ProblemsPanel;
class ProjectController;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    friend class IntegrationTests;
    void buildCentralWidget();
    void buildMenusAndToolbar();
    void buildStatusBar();
    void connectDocumentSignals();
    void connectAnalysisSignals();

    void saveLayout();
    void restoreLayout();
    void resetLayout();

    void notImplemented(const QString &feature);

    bool confirmDiscardIfDirty();
    void doNewProject();
    void doImportC3D8Input();
    void doOpenProject();
    bool doSaveProject();
    bool doSaveProjectAs();
    void doExportSolverJson();
    void doValidate();
    void doRun();
    void doStop();
    void doFitCurrentView();
    void doDrawPatch();
    void acceptDrawnPatch(const QRectF &bounds);
    void onPatchPicked(const QUuid &id);
    void doExportResultsCsv();
    void doExportResultsPng();
    void doOpenOutputFolder(const QString &path);

    void onProjectReplaced();
    void onModelEdited();
    void rebuildMeshAndScene();
    void updateStatusBar();
    void updateRunActionStates();
    void onCenterTabChanged(int index);
    void onWorkflowStepChanged(int pageIndex);

    void onNodePicked(vtkIdType solverNodeId);
    void onElementPicked(vtkIdType solverElementId);
    void onSelectionCleared();

    void onRunStarted(const QString &runId);
    void onRunFinished(const QString &runId, bool success, const QString &errorMessage);
    void onThemeChanged(UiStyle::Theme theme);

    // Left / center / right / bottom
    ProjectController *m_controller = nullptr;
    ProjectPanel *m_projectPanel = nullptr;
    QTabWidget *m_centerTabs = nullptr;
    ViewportWidget *m_modelViewport = nullptr;
    ViewportWidget *m_meshViewport = nullptr;
    ViewportWidget *m_resultsViewport = nullptr;
    InspectorPanel *m_inspectorPanel = nullptr;
    ProblemsPanel *m_problemsPanel = nullptr;

    QDockWidget *m_projectDock = nullptr;
    QDockWidget *m_inspectorDock = nullptr;
    QDockWidget *m_problemsDock = nullptr;

    QSplitter *m_mainSplitter = nullptr;   // legacy compatibility
    QSplitter *m_verticalSplitter = nullptr; // legacy compatibility

    // Status bar
    QLabel *m_stateLabel = nullptr;
    QLabel *m_problemsLabel = nullptr;
    QLabel *m_demoDataLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;

    // Actions whose enabled state depends on validation / run state
    QAction *m_runAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_newAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_importAction = nullptr;
    QAction *m_undoAction = nullptr, *m_redoAction = nullptr, *m_drawAction = nullptr;
    QAction *m_csvAction = nullptr, *m_pngAction = nullptr;
    QAction *m_validateAction = nullptr;
    QAction *m_fitViewAction = nullptr;
    QActionGroup *m_themeGroup = nullptr;
    QAction *m_themeGlassDarkAction = nullptr;
    QAction *m_themeGlassLightAction = nullptr;
    QAction *m_themeClassicAction = nullptr;
    bool m_closeAfterRun = false;
    bool m_syncingNav = false;

    ValidationIssues m_lastIssues;
};

} // namespace femgui
