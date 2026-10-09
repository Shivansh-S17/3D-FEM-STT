#pragma once

#include <QMainWindow>

class QSplitter;
class QTabWidget;
class QLabel;
class QAction;

namespace femgui {

class ProjectPanel;
class ViewportWidget;
class InspectorPanel;
class ProblemsPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildCentralWidget();
    void buildMenusAndToolbar();
    void buildStatusBar();
    void wireDemoBehavior();

    void saveLayout();
    void restoreLayout();
    void resetLayout();

    void notImplemented(const QString &feature);
    ViewportWidget *activeViewport() const;

    // Left / center / right / bottom
    ProjectPanel *m_projectPanel = nullptr;
    QTabWidget *m_centerTabs = nullptr;
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
};

} // namespace femgui
