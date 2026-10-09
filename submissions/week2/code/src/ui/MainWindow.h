#pragma once

#include <QMainWindow>
#include <QRectF>
#include <QUuid>

#include <vtkType.h>

#include "core/ValidationIssue.h"

class QSplitter;
class QTabWidget;
class QLabel;
class QAction;

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
    ViewportWidget *activeViewport() const;

    bool confirmDiscardIfDirty();
    void doNewProject();
    void doOpenProject();
    bool doSaveProject();
    bool doSaveProjectAs();
    void doExportSolverJson();
    void doValidate();
    void doRun();
    void doStop();
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

    void onNodePicked(vtkIdType solverNodeId);
    void onElementPicked(vtkIdType solverElementId);
    void onSelectionCleared();

    void onRunStarted(const QString &runId);
    void onRunFinished(const QString &runId, bool success, const QString &errorMessage);

    // Left / center / right / bottom
    ProjectController *m_controller = nullptr;
    ProjectPanel *m_projectPanel = nullptr;
    QTabWidget *m_centerTabs = nullptr;
    ViewportWidget *m_modelViewport = nullptr;
    ViewportWidget *m_meshViewport = nullptr;
    ViewportWidget *m_resultsViewport = nullptr;
    InspectorPanel *m_inspectorPanel = nullptr;
    ProblemsPanel *m_problemsPanel = nullptr;

    QSplitter *m_mainSplitter = nullptr;   // left | center | right
    QSplitter *m_verticalSplitter = nullptr; // (mainSplitter) over (problemsPanel)

    // Status bar
    QLabel *m_stateLabel = nullptr;
    QLabel *m_unitsLabel = nullptr;
    QLabel *m_nodesLabel = nullptr;
    QLabel *m_elementsLabel = nullptr;
    QLabel *m_problemsLabel = nullptr;
    QLabel *m_demoDataLabel = nullptr;

    // Actions whose enabled state depends on validation / run state
    QAction *m_runAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_newAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_undoAction = nullptr, *m_redoAction = nullptr, *m_drawAction = nullptr;
    QAction *m_csvAction = nullptr, *m_pngAction = nullptr;
    bool m_closeAfterRun = false;

    ValidationIssues m_lastIssues;
};

} // namespace femgui
