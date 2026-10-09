#pragma once

#include <QWidget>

#include "core/ValidationIssue.h"

class QTreeWidget;
class QTreeWidgetItem;
class QStackedWidget;
class QLabel;
class QLineEdit;
class QDoubleSpinBox;
class QTableWidget;
class QPushButton;

namespace femgui {

class ProjectController;
class ScientificNumberEdit;

// Left panel: workflow tree plus bound editors for plate and C3D8 models
// . Pages are built once and kept (never recreated per click),
// fixing the prior placeholder implementation's page-leak on selection.
class ProjectPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectPanel(QWidget *parent = nullptr);

    // Rebinds every page to the controller's current document. Call once
    // after construction and again whenever ProjectController::projectReplaced()
    // fires (New/Open).
    void bindToController(ProjectController *controller);

public slots:
    void setValidationIssues(const ValidationIssues &issues);
    void refreshAll();
    void refreshSummaries();
    void navigateToField(const QString &fieldKey);
    void selectPatch(const QUuid &id);

signals:
    void statusMessage(const QString &message);
    // MainWindow owns the viewports; the panel asks for a rebuild rather
    // than touching VTK actors itself.
    void modelEdited();
    void openOutputFolderRequested(const QString &path);
    void loadRunRequested(const QString &runId);
    void drawPatchRequested();
    void patchSelected(QUuid id);

private:
    friend class IntegrationTests;
    void populateTree();
    QWidget *buildOverviewPage();
    QWidget *buildMaterialPage();
    QWidget *buildFoundationPage();
    QWidget *buildCoordinatesPage();
    QWidget *buildMeshPage();
    QWidget *buildLoadsPage();
    QWidget *buildAnalysisPage();
    QWidget *buildResultsPage();

    void refreshOverviewPage();
    void refreshMaterialPage();
    void refreshFoundationPage();
    void refreshCoordinatesPage();
    void refreshMeshPage();
    void refreshLoadsPage();
    void refreshAnalysisPage();

    void onCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void applyMaterial();
    void applyFoundation();
    void applyCoordinates();
    void onLoadsTableCellChanged(int row, int column);
    void addPatch();
    void editSelectedPatch();
    void duplicateSelectedPatch();
    void deleteSelectedPatch();
    void checkBackend();

    ProjectController *m_controller = nullptr;
    QTreeWidget *m_tree = nullptr;
    QStackedWidget *m_details = nullptr;

    // Overview
    QLineEdit *m_nameEdit = nullptr;
    QLabel *m_revisionLabel = nullptr;
    QLabel *m_countsLabel = nullptr;
    QLabel *m_readinessLabel = nullptr;
    QLabel *m_backendLabel = nullptr;

    // Material
    ScientificNumberEdit *m_emodSpin = nullptr;
    ScientificNumberEdit *m_nuSpin = nullptr;
    ScientificNumberEdit *m_thicknessSpin = nullptr;

    // Foundation
    ScientificNumberEdit *m_kxSpin = nullptr;
    ScientificNumberEdit *m_kySpin = nullptr;
    ScientificNumberEdit *m_kzSpin = nullptr;

    // Coordinates
    QTableWidget *m_xTable = nullptr;
    QTableWidget *m_yTable = nullptr;
    QTableWidget *m_zTable = nullptr;

    // Mesh
    QLabel *m_meshSummaryLabel = nullptr;

    // Loads
    QTableWidget *m_loadsTable = nullptr;
    bool m_updatingLoadsTable = false;

    // Analysis
    QLineEdit *m_pythonPathEdit = nullptr;
    QLineEdit *m_driverPathEdit = nullptr;
    QLabel *m_backendCheckLabel = nullptr;
    QTableWidget *m_runHistoryTable = nullptr;

    ValidationIssues m_lastIssues;
    bool m_hasValidated = false;
};

} // namespace femgui
